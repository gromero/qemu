from __future__ import print_function


try:
    import gdb
except ModuleNotFoundError:
    from sys import exit
    exit("This script must be launched via tests/guest-debug/run-test.py!")
import logging
import os
import re
import subprocess
import sys
import unittest


from test_gdbstub import arg_parser, main, report


QEMU_SOURCE = "/mnt/git/qemu_rr_fix"
# qemu_test
sys.path.insert(0, QEMU_SOURCE + "/tests/functional/")
# pycotap
sys.path.insert(0, QEMU_SOURCE + "/build/pyvenv/lib/python3.10/site-packages/")
# qemu.machine
sys.path.insert(0, QEMU_SOURCE + "/python")


from qemu_test import LinuxKernelTest, get_qemu_img
from qemu_test.ports import Ports
from qemu_test import Asset, skipIfMissingImports, skipFlakyTest


class ReverseDebugging(LinuxKernelTest):
    """
    Test GDB reverse debugging commands: reverse step and reverse continue.
    Recording saves the execution of some instructions and makes an initial
    VM snapshot to allow reverse execution.
    Replay saves the order of the first instructions and then checks that they
    are executed backwards in the correct order.
    After that the execution is replayed to the end, and reverse continue
    command is checked by setting several breakpoints, and asserting
    that the execution is stopped at the last of them.
    """

    timeout = 10
    STEPS = 10
    # endian_is_le = True

    @staticmethod
    def gdb_connect(host, port):
        # Set debug on connection to get the qSupport string
        gdb.execute("set debug remote 1", False, True)
        r = gdb.execute(f"target remote {host}:{port}", False, True)
        gdb.execute("set debug remote 0", False, True)

        return r

    def run_vm(self, record, shift, args, replay_path, image_path, port):
        logger = logging.getLogger('replay')
        vm = self.get_vm(name='record' if record else 'replay')
        vm.set_console()
        if record:
            logger.info('recording the execution...')
            mode = 'record'
        else:
            logger.info('replaying the execution...')
            mode = 'replay'
            vm.add_args('-gdb', 'tcp::%d' % port, '-S')
        vm.add_args('-icount', 'shift=%s,rr=%s,rrfile=%s,rrsnapshot=init' %
                    (shift, mode, replay_path),
                    '-net', 'none')
        vm.add_args('-drive', 'file=%s,if=none' % image_path)
        if args:
            vm.add_args(*args)
        f = open("/tmp/avo", "a")
        f.write(str(" ".join(vm._qemu_full_args)))
        f.close()
        vm.launch()

        return vm

    @staticmethod
    def get_pc():
        val = gdb.parse_and_eval("$pc")
        pc = int(val)

        return pc

    def check_pc(self, addr):
        pc = self.get_pc()
        if pc != addr:
            self.fail('Invalid PC (read %x instead of %x)' % (pc, addr))

    @staticmethod
    def gdb_step():
        # g.cmd(b's', b'T05thread:01;')
        gdb.execute("stepi")

    @staticmethod
    def gdb_bstep():
        # g.cmd(b'bs', b'T05thread:01;')
        gdb.execute("reverse-stepi")

    @staticmethod
    def vm_get_icount(vm):
        return vm.qmp('query-replay')['return']['icount']

    def reverse_debugging(self, shift=7, args=None):
        logger = logging.getLogger('replay')

        # create qcow2 for snapshots
        logger.info('creating qcow2 image for VM snapshots')
        image_path = os.path.join("/tmp", 'disk.qcow2')
        qemu_img = get_qemu_img(self)
        if qemu_img is None:
            self.skipTest('Could not find "qemu-img", which is required to '
                          'create the temporary qcow2 image')
        cmd = '%s create -f qcow2 %s 128M' % (qemu_img, image_path)
        f =  open("/tmp/avo", "a")
        f.write(f"COMMAND: {cmd}\n");
        subprocess.run(cmd, shell=True)

        replay_path = os.path.join(self.workdir, 'replay.bin')

        # record the log
        vm = self.run_vm(True, shift, args, replay_path, image_path, -1)
        while self.vm_get_icount(vm) <= self.STEPS:
            pass
        last_icount = self.vm_get_icount(vm)
        vm.shutdown()

        logger.info("recorded log with %s+ steps" % last_icount)

        # replay and run debug commands
        with Ports() as ports:
            port = ports.find_free_port()
            vm = self.run_vm(False, shift, args, replay_path, image_path, port)
        logger.info('connecting to gdbstub')
        r = self.gdb_connect('127.0.0.1', port)
        # r = g.cmd(b'qSupported')
        # if b'qXfer:features:read+' in r:
        #     g.cmd(b'qXfer:features:read:target.xml:0,ffb')
        if 'ReverseStep+' not in r:
            self.fail('Reverse step is not supported by QEMU')
        if 'ReverseContinue+' not in r:
            self.fail('Reverse continue is not supported by QEMU')

        logger.info('stepping forward')
        steps = []
        # record first instruction addresses
        for _ in range(self.STEPS):
            pc = self.get_pc()
            logger.info('saving position %x' % pc)
            steps.append(pc)
            self.gdb_step()

        # visit the recorded instruction in reverse order
        logger.info('stepping backward')
        for addr in steps[::-1]:
            self.gdb_bstep()
            self.check_pc(addr)
            logger.info('found position %x' % addr)

        # visit the recorded instruction in forward order
        logger.info('stepping forward')
        for addr in steps:
            self.check_pc(addr)
            self.gdb_step()
            logger.info('found position %x' % addr)

        # set breakpoints for the instructions just stepped over
        logger.info('setting breakpoints')
        for addr in steps:
            # hardware breakpoint at addr with len=1
            # g.cmd(b'Z1,%x,1' % addr, b'OK')
            gdb.execute(f"break *{hex(addr)}")

        # this may hit a breakpoint if first instructions are executed
        # again
        logger.info('continuing execution')
        vm.qmp('replay-break', icount=last_icount - 1)
        # continue - will return after pausing
        # This could stop at the end and get a T02 return, or by
        # re-executing one of the breakpoints and get a T05 return.
        # g.cmd(b'c')
        gdb.execute("continue")
        if self.vm_get_icount(vm) == last_icount - 1:
            logger.info('reached the end (icount %s)' % (last_icount - 1))
        else:
            logger.info('hit a breakpoint again at %x (icount %s)' %
                        (self.get_pc(g), self.vm_get_icount(vm)))

        logger.info('running reverse continue to reach %x' % steps[-1])
        # reverse continue - will return after stopping at the breakpoint
        gdb.execute("reverse-continue")
        # g.cmd(b'bc', b'T05thread:01;')

        # assume that none of the first instructions is executed again
        # breaking the order of the breakpoints
        self.check_pc(steps[-1])
        logger.info('successfully reached %x' % steps[-1])

        logger.info('exiting gdb and qemu')

        f.close()

        vm.shutdown()


class ReverseDebugging_AArch64(ReverseDebugging):

    # REG_PC = 32

    KERNEL_ASSET = Asset(
        ('https://archives.fedoraproject.org/pub/archive/fedora/linux/'
         'releases/29/Everything/aarch64/os/images/pxeboot/vmlinuz'),
        '7e1430b81c26bdd0da025eeb8fbd77b5dc961da4364af26e771bd39f379cbbf7')

    # @skipFlakyTest("https://gitlab.com/qemu-project/qemu/-/issues/2921")
    def test_aarch64_virt(self):
        self.set_machine('virt')
        self.cpu = 'cortex-a53'
        kernel_path = self.KERNEL_ASSET.fetch()
        self.reverse_debugging(args=('-kernel', kernel_path))


def run_test():
    # sys.argv = ["reverse_debugging.py"]
    # print("=====>", sys.argv)
    # rd = ReverseDebugging_AArch64()
    # rd.reverse_debugging("reverse_debugging")
    # ReverseDebugging.main()
    print("auto_connect", auto_connect)
    report(True, "Test is OK")

# print(__name__)
# print(sys.modules)


if __name__ == "__main__":
    # sys.argv = ["reverse_debugging.py"]
    sys.argv = ["test_reverse.py"]
    ReverseDebugging_AArch64.main()
    # unittest.main(module=test_reverse, exit=False)


# main(run_test, expected_arch="aarch64", auto_connect=False)
