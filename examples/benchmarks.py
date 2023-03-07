import optparse
import os
import subprocess
import filecmp
import time
from typing import Type


class BaseExec:
    def __init__(self, root, cmd):
        self.name = None
        self.root = root
        self.args = cmd
        self.rc = None
        self.out = None
        self.err = None
        self.time = None

    def run(self):
        pass


class QEMUExec(BaseExec):
    bin_path = "qemu-riscv32"

    def __init__(self, root, cmd):
        super().__init__(root, cmd)
        self.name = "qemu"

    def run(self):
        pargs = [QEMUExec.bin_path] + self.args
        timer = time.time()
        p = subprocess.Popen(pargs, cwd=self.root,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.out, self.err = p.communicate()
        timer = time.time() - timer
        self.rc = p.returncode
        self.time = timer


class RVDBTExec(BaseExec):
    build_dir = None

    def __init__(self, root, cmd):
        super().__init__(root, cmd)
        self.name = "rvdbt"

    def run(self):
        pargs = [RVDBTExec.build_dir + "/bin/elfrun",
                 "--cache=dbtcache",
                 "--fsroot=" + self.root,
                 "--"] + self.args
        timer = time.time()
        p = subprocess.Popen(pargs, cwd=RVDBTExec.build_dir,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.out, self.err = p.communicate()
        timer = time.time() - timer
        self.rc = p.returncode
        self.time = timer


class Benchmark:
    def __init__(self, root, args, cmp_out=False, ofile=None):
        self.root = root
        self.args = args
        self.cmp_out = cmp_out
        self.ofile = self.root + "/" + ofile if ofile is not None else None

    def get_ofile(self, exec):
        assert (self.ofile is not None)
        return self.ofile + "." + exec.name

    def launch_with(self, exec_type: Type[BaseExec]):
        exec = exec_type(self.root, self.args)
        exec.run()
        if self.ofile is not None:
            os.rename(self.ofile, self.get_ofile(exec))
        return exec

    def verify(self, exec: BaseExec, exec_ref: BaseExec):
        if exec_ref.rc != exec.rc:
            return "retcode"

        if self.cmp_out and exec_ref.out != exec.out:
            return "stdout"

        if self.ofile is not None:
            if not filecmp.cmp(self.get_ofile(exec_ref), self.get_ofile(exec)):
                return "outfile"

        return None

    def result(self, exec: BaseExec, exec_ref: BaseExec = None):
        if exec_ref is None:
            res = "ok"
        else:
            res = self.verify(exec, exec_ref)
            res = (res, "ok")[res is None]
        res += ":" + str(exec.rc)
        report = [res, exec.time]
        if exec_ref is not None:
            report += [f"{exec_ref.time / exec.time:.3f}"]
        return report


# mibench.automotive
def GetBenchmarks_Automotive(prebuilts_dir):
    b: list[Benchmark] = []
    root = os.path.join(prebuilts_dir + "/automotive")
    b.append(Benchmark(root + "/basicmath", ["basicmath_small"], True))
    b.append(Benchmark(root + "/basicmath", ["basicmath_large"]))
    b.append(Benchmark(root + "/bitcnts", ["bitcnts", "3125000"]))
    b.append(Benchmark(root + "/qsort",
             ["qsort_small", "input_small.dat"], True))
    b.append(Benchmark(root + "/qsort",
             ["qsort_large", "input_large.dat"], True))
    b.append(Benchmark(root + "/susan",
             ["susan", "input_large.pgm", "_bout", "-s"], False, "_bout"))
    return b


def RunTests(opts):
    benchmarks: list[Benchmark] = []
    benchmarks += GetBenchmarks_Automotive(opts.prebuilts_dir)

    for b in benchmarks:
        print(b.root + " " + " ".join(b.args))
        ref_exec = b.launch_with(QEMUExec)
        dbt_exec = b.launch_with(RVDBTExec)
        print([ref_exec.name] + b.result(ref_exec))
        print([dbt_exec.name] + b.result(dbt_exec, ref_exec))


def main():
    op = optparse.OptionParser()
    op.add_option("--build-dir", dest="build_dir")
    op.add_option("--prebuilts", dest="prebuilts_dir")

    (opts, args) = op.parse_args()
    if not opts.build_dir or not opts.prebuilts_dir:
        op.error("usage")

    RVDBTExec.build_dir = opts.build_dir

    RunTests(opts)


main()
