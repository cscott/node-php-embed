import sys
import tarfile
import os
import subprocess

tarball = os.path.abspath(sys.argv[1])
dirname = os.path.abspath(sys.argv[2])
print "Extracting", tarball, "to", dirname
tfile = tarfile.open(tarball,'r:gz');
tfile.extractall(dirname)
# Optionally, apply a patch.  This can be useful when debugging
# crashes deep inside php.
if os.path.isfile(tarball + '.patch'):
    subprocess.check_call(['patch', '-d', dirname, '-i', tarball + '.patch', '-p', '0']);
os.utime(os.path.join(dirname, sys.argv[3]), None) # touch
sys.exit(0)
