#!/usr/local/bin/python

import os
import sys
import time
import requests
import threading
import zipfile

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

def download(link, filelocation):
    r = requests.get(link, stream=True)
    with open(filelocation, 'wb') as f:
        for chunk in r.iter_content(1024):
            if chunk:
                f.write(chunk)

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
    # Remove the entry
    del downloads[zip_file]

    try:
        # Extract the downloaded zip
        print('Extracting ' + zip_file + '...')
        sys.stdout.flush()
        with zipfile.ZipFile(zip_file, 'r') as zip_ref:
            zip_ref.extractall()

        print('Cleaning up "' + zip_file + '"...')
        sys.stdout.flush()
        if os.path.isfile(zip_file): os.remove(zip_file)

    except Exception as e:
        print(e)
        pass

