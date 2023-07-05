// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <openvdb/openvdb.h>
#include <openvdb/tools/Count.h>
#define OPENVDB_PROFILE_PCA
#include <openvdb/points/PointRasterizeSDF.h>
#include <openvdb/util/logging.h>


int
main(int, char *[])
{
    using namespace openvdb;
    using namespace openvdb::points;

    try {
        openvdb::initialize();

        openvdb::io::File file("/Users/nicholasa/dev/openvdb_caches/waterfall_points.vdb");
        file.open(false);
        auto grids = file.getGrids();
        file.close();
        auto points = StaticPtrCast<PointDataGrid>(grids->front());

        points::PcaSettings s;
        s.searchRadius = float(points->voxelSize()[0])*2.0f;
        points::PcaAttributes a;
        points::pca(*points,s,a);

        points::EllipsoidSettings<> es;
        es.pca = a;
        es.radiusScale = points->voxelSize()[0];
        es.sphereScale = 0.2f;
        auto outgrids = points::rasterizeSdf(*points, es);
        outgrids.push_back(points);

        openvdb::io::File out("/Users/nicholasa/dev/openvdb_caches/out.vdb");
        out.write(outgrids);

    }
    catch (const std::exception& e) {
        OPENVDB_LOG_FATAL(e.what());
        return EXIT_FAILURE;
    }
    catch (...) {
        OPENVDB_LOG_FATAL("Exception caught (unexpected type)");
        std::terminate();
    }

    return 0;
}
