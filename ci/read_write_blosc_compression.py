#!/usr/local/bin/python

import os
import sys
import time
import requests
import threading
import zipfile
import subprocess
import glob

# Disable delay loading to fully test full compress/decompress
os.environ['OPENVDB_DISABLE_DELAYED_LOAD'] = "1"

vdb_aliases = {
    'torus_knot.vdb' : 'torus_knot_helix',
    'smoke1.vdb' : 'smoke',
}

import pyopenvdb as vdb


def diff(src_file, dst_file):
    print('Comparing grids with AX...')
    sys.stdout.flush()

    grids = vdb.readAllGridMetadata(src_file)
    a = grids[0].valueTypeName + '@' + grids[0].name
    grids = vdb.readAllGridMetadata(dst_file)
    b = grids[0].valueTypeName + '@' + grids[0].name
    assert(a!=b)

    ax_code = '''
        if ({a} != {b}) print({a}-{b});
    '''.format(a=a, b=b)

    ax_cmd = ['vdb_ax', src_file, dst_file, '-s', ax_code]
    process = subprocess.Popen(ax_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    out, err = process.communicate()

    if out:
        print('VDBs from "' + src_file + '" and "' + dst_file + '" differ.')


# Process files
vdbs = glob.glob('*.vdb')
assert(len(vdbs) > 0)

for src_file in vdbs:
    print('-----------------------------------------')
    print('Processing "' + src_file + '"')
    sys.stdout.flush()

    # Get the archive vdb file name (some file are named differently to the archive)
    name = src_file.replace('_blosc', '')
    name = vdb_aliases.get(name)
    if not name: name = src_file.split('.vdb')[0]

    dst_file = name + '_output.vdb'

    try:
        # Read/Write - ignore files with multiple grids as we want to check file
        # sizes after write and we currently don't write out multiple grids
        grids = vdb.readAll(src_file)
        if len(grids) > 2:
            print(grids)
            raise RuntimeError('Multiple grids in file "' + src_file + '", refusing to read.')

        grids[0][0].name += '_output'
        vdb.write(dst_file, grids[0])
        # Also try and read the compressed grids to make sure they are legit
        compressed = vdb.readAll(dst_file)

        src_size = os.path.getsize(src_file)
        dst_size = os.path.getsize(dst_file)

        print(src_file + ' read/write successful.')
        print('  input size: [' + str(src_size) + ']')
        print('  outpt size: [' + str(dst_size) + ']')
        print('  comp ratio: [' + str(src_size/float(dst_size)) + ']')
        sys.stdout.flush()

    except Exception as e:
        print(e)
        pass

    if os.path.isfile(src_file) and os.path.isfile(dst_file):
        diff(src_file, dst_file)

    # Cleanup
    print('Cleaning up "' + name + '" files...')
    sys.stdout.flush()
    if os.path.isfile(src_file): os.remove(src_file)
    if os.path.isfile(dst_file): os.remove(dst_file)

