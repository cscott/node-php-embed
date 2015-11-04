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
if len(sys.argv) >= 6:
    print "Renaming", os.path.join(dirname, sys.argv[4]), "to", os.path.join(dirname, sys.argv[5])
    subprocess.call(['/bin/rm', '-rf', os.path.join(dirname, sys.argv[5])])
    os.rename(os.path.join(dirname, sys.argv[4]),
               os.path.join(dirname, sys.argv[5]));
sys.exit(0)
