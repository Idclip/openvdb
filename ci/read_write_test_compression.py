#!/usr/local/bin/python

import os
import sys
import time
import requests
import threading
import zipfile
import subprocess

# Disable delay loading to fully test full compress/decompress
os.environ['OPENVDB_DISABLE_DELAYED_LOAD'] = "1"

import pyopenvdb as vdb

if hasattr(vdb, 'ax'):
    print('ax enabled')
    diff_with_ax = True
else:
    print('ax disabled')
    diff_with_ax = False

vdb_urls = [
    'https://artifacts.aswf.io/io/aswf/openvdb/models/armadillo.vdb/1.0.0/armadillo.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/buddha.vdb/1.0.0/buddha.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/armadillo.vdb/1.0.0/armadillo.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/buddha.vdb/1.0.0/buddha.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/bunny.vdb/1.0.0/bunny.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/bunny_cloud.vdb/1.0.0/bunny_cloud.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/crawler.vdb/1.0.0/crawler.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/cube.vdb/1.0.0/cube.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/dragon.vdb/1.0.0/dragon.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/emu.vdb/1.0.0/emu.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/explosion.vdb/1.0.0/explosion.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/fire.vdb/1.0.0/fire.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/icosahedron.vdb/1.0.0/icosahedron.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/iss.vdb/1.0.0/iss.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/smoke1.vdb/1.0.0/smoke1.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/smoke2.vdb/1.0.0/smoke2.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/space.vdb/1.0.0/space.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/sphere.vdb/1.0.0/sphere.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/torus.vdb/1.0.0/torus.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/torus_knot.vdb/1.0.0/torus_knot.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/utahteapot.vdb/1.0.0/utahteapot.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/venusstatue.vdb/1.0.0/venusstatue.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/boat_points.vdb/1.0.0/boat_points.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/bunny_points.vdb/1.0.0/bunny_points.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/sphere_points.vdb/1.0.0/sphere_points.vdb-1.0.0.zip',
    'https://artifacts.aswf.io/io/aswf/openvdb/models/waterfall_points.vdb/1.0.0/waterfall_points.vdb-1.0.0.zip'
]

vdb_aliases = {
    'torus_knot.vdb-1.0.0.zip' : 'torus_knot_helix',
    'smoke1.vdb-1.0.0.zip' : 'smoke',
}

def download(link, filelocation):
    r = requests.get(link, stream=True)
    with open(filelocation, 'wb') as f:
        for chunk in r.iter_content(1024):
            if chunk:
                f.write(chunk)

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

# Init downloads

downloads = dict()

for url in vdb_urls:
    zip_filename = os.path.basename(url)
    print('Initiating download "' + url + '"')
    download_thread = threading.Thread(target=download, args=(url,zip_filename))
    download_thread.start()
    downloads[zip_filename] = download_thread

sys.stdout.flush()

# Process files

while downloads:
    zip_file = None
    while not zip_file:
        time.sleep(1)
        for file, thread in downloads.items():
            if not thread.is_alive():
                thread.join()
                zip_file = file
                break

    print('-----------------------------------------')
    print('Processing "' + zip_file + '"')
    sys.stdout.flush()
    # Remove the entry
    del downloads[zip_file]

    src_file = ''
    dst_file = ''
    # Get the archive vdb file name (some file are named differently to the archive)
    name = vdb_aliases.get(zip_file)
    if not name: name = zip_file.split('.vdb')[0]

    try:
        # Extract the downloaded zip
        print('Extracting ' + zip_file + '...')
        sys.stdout.flush()
        with zipfile.ZipFile(zip_file, 'r') as zip_ref:
            zip_ref.extractall()

        # Get source/destination files
        src_file = name + '.vdb'
        dst_file = name + '_blosc_compressed.vdb'

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

        print(zip_file + ' read/write successful.')
        print('  input size: [' + str(src_size) + ']')
        print('  outpt size: [' + str(dst_size) + ']')
        print('  comp ratio: [' + str(src_size/float(dst_size)) + ']')
        sys.stdout.flush()

    except Exception as e:
        print(e)
        pass

    if diff_with_ax:
        if os.path.isfile(src_file) and os.path.isfile(dst_file):
            diff(src_file, dst_file)

    # Cleanup
    print('Cleaning up "' + name + '" files...')
    sys.stdout.flush()
    if os.path.isfile(src_file): os.remove(src_file)
    if os.path.isfile(dst_file): os.remove(dst_file)
    if os.path.isfile(zip_file): os.remove(zip_file)

