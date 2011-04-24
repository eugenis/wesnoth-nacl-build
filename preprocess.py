#!/usr/bin/python

import os, sys, re

def process_dir_or_file(base, path, f_out):
    if os.path.isdir(path):
#        print >>f_out, '# start ' + path
        for child in os.listdir(path):
            process_dir_or_file(base, os.path.join(path, child), f_out)
#        print >>f_out, '# end ' + path
        return True
    elif os.path.isfile(path):
#        print >>f_out, '# start ' + path
        process_file(base, path, f_out)
#        print >>f_out, '# end ' + path
        return True
    else:
#        print >>f_out, "# WARNING: " + path + " not found"
        return False

def process_file(base, path, f_out):
    f_in = open(path)
    for line in f_in:
	if line[-1] == '\n':
	  line = line[:-1]
#        line = line.rstrip()
        m = re.match(r'[ \t]*{([^{}]+)}[ \t]*', line)
        if m:
            include = m.group(1)
            if not process_dir_or_file(base, os.path.join(base, include), f_out):
                print >>f_out, line
        else:
            print >>f_out, line

if __name__ == '__main__':
    process_file(sys.argv[2], sys.argv[1], sys.stdout)
