// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: MPL-2.0

///////////////////////////////////////////////////////////////////////////
//
/// @file SOP_OpenVDB_Points_Surfacer.cc
///
/// @author Richard Jones, Nick Avramoussis, Franciso Gochez
///
/// @brief Surface VDB Points into a VDB Level Set using ellipse


#include <openvdb/openvdb.h>
#include <openvdb/Grid.h>
#include <openvdb/tools/LevelSetRebuild.h>
#include <openvdb/points/PointDataGrid.h>
#include <openvdb/points/PointDelete.h>
#include <openvdb/points/PointStatistics.h>
#define OPENVDB_PROFILE_PCA
#include <openvdb/points/PointRasterizeSDF.h>
#include <openvdb/points/PrincipalComponentAnalysis.h>
#include <openvdb/util/NullInterrupter.h>

#include <houdini_utils/ParmFactory.h>
#include <openvdb_houdini/Utils.h>
#include <openvdb_houdini/PointUtils.h>
#include <openvdb_houdini/SOP_NodeVDB.h>

#include <CH/CH_Manager.h>
#include <PRM/PRM_Parm.h>

namespace hvdb = openvdb_houdini;
namespace hutil = houdini_utils;

class SOP_OpenVDB_Points_Surfacer: public openvdb_houdini::SOP_NodeVDB
{
public:
    SOP_OpenVDB_Points_Surfacer(OP_Network*, const char* name, OP_Operator*);
    virtual ~SOP_OpenVDB_Points_Surfacer() {}

    static OP_Node* factory(OP_Network*, const char* name, OP_Operator*);

protected:
    OP_ERROR cookVDBSop(OP_Context&) override;
    bool updateParmsFlags() override;
};


////////////////////////////////////////

namespace
{

using SupportedGridT =
    openvdb::TypeList<bool, int32_t, int64_t, float, double,
        openvdb::Vec3f, openvdb::Vec3d, openvdb::Vec3i>;

void setInclusionGroup(openvdb::points::PointDataTree& tree,
                       const std::vector<std::string>& includeGroups,
                       const std::vector<std::string>& excludeGroups,
                       const std::string& inclusionGroup)
{
    auto leaf = tree.cbeginLeaf();

    assert(!leaf->hasGroup(inclusionGroup));
    openvdb::points::appendGroup(tree, inclusionGroup);

    openvdb::points::MultiGroupFilter filter(includeGroups, excludeGroups, leaf->attributeSet());
    openvdb::points::setGroupByFilter(tree, inclusionGroup, filter);

}

inline double
getAverageRadius(const openvdb::points::PointDataTree& tree,
                 const std::string& name,
                 const std::vector<std::string>& include,
                 const std::vector<std::string>& exclude)
{
    auto iter = tree.cbeginLeaf();
    if (!iter) return 0.0;

    if (exclude.empty() && include.empty()) {
        return openvdb::points::evalAverage<float>(tree, name);
    }
    else if (exclude.empty() && include.size() == 1) {
        const openvdb::points::GroupFilter filter(include.front(), iter->attributeSet());
        return openvdb::points::evalAverage<float,
            openvdb::points::UnknownCodec,
            openvdb::points::GroupFilter>(tree, name, filter);
    }
    else {
        const openvdb::points::MultiGroupFilter filter(include, exclude, iter->attributeSet());
        return openvdb::points::evalAverage<float,
            openvdb::points::UnknownCodec,
            openvdb::points::MultiGroupFilter>(tree, name, filter);
    }
}

}

////////////////////////////////////////

void
newSopOperator(OP_OperatorTable* table)
{
    if (table == nullptr) return;

    hutil::ParmList parms;

    // INPUT PARMS

    parms.add(hutil::ParmFactory(PRM_STRING, "group", "Group")
        .setChoiceList(&hutil::PrimGroupMenu)
        .setTooltip("Specify a subset of the input point VDBs to surface.")
        .setDocumentation(
            "A subset of the input VDB Points primitives to be processed"));

    // SURFACE PARMS
    parms.add(hutil::ParmFactory(PRM_STRING, "surfacevdbname", "Output Surface VDB")
        .setDefault("surface")
        .setTooltip("The name of the surface VDB to be created."));

    parms.add(hutil::ParmFactory(PRM_STRING, "referencegroup", "Reference VDB")
        .setChoiceList(&hutil::PrimGroupMenuInput2)
        .setTooltip(
            "Give the output VDB the same orientation and voxel size as the selected VDB."));

    parms.add(hutil::ParmFactory(PRM_STRING, "vdbpointsgroups", "VDB Points Groups")
        .setChoiceList(&hvdb::VDBPointsGroupMenuInput1)
        .setDefault("")
        .setHelpText("Specify VDB Points Groups to use. (Default is all groups)"));

    parms.add(hutil::ParmFactory(PRM_TOGGLE, "keep", "Keep VDB Points")
        .setDefault(PRMzeroDefaults)
        .setTooltip("If enabled, VDB point grids will not be removed from the geometry stream."));

    parms.add(hutil::ParmFactory(PRM_FLT_J, "voxelsize", "Voxel Size")
        .setDefault(PRMpointOneDefaults)
        .setRange(PRM_RANGE_RESTRICTED, 1e-5, PRM_RANGE_UI, 5)
        .setTooltip("Uniform voxel edge length in world units.  "
            "Decrease the voxel size to increase the volume resolution."));

    parms.add(hutil::ParmFactory(PRM_INT_J, "halfbandvoxels", "Half-Band Voxels")
        .setDefault(PRMthreeDefaults)
        .setRange(PRM_RANGE_RESTRICTED, 1, PRM_RANGE_UI, 10)
        .setTooltip("Half the width of the narrow band in voxel units.  "
            "The default value 3 is recommended for level set volumes. "
            "For the Average Position mode, the width of the exterior half band "
            "*may* be smaller than the specified half band if the Influence radius "
            "is less than the equivalent world space half band distance."));

    parms.add(hutil::ParmFactory(PRM_TOGGLE, "rebuildlevelset", "Rebuild Level Set")
        .setDefault(PRMoneDefaults)
        .setTooltip("Rebuild the level set after running the surfacing algorithm"));

    parms.add(hutil::ParmFactory(PRM_ORD, "mode", "Mode")
        .setDefault(PRMzeroDefaults)
        .setChoiceListItems(PRM_CHOICELIST_SINGLE, {
            "spheres", "Spheres",
            "zhubrid", "Zhu Bridson",
            "ellips", "Ellipsoids"
        })
        .setTooltip("")
        .setDocumentation(""));

    parms.add(hutil::ParmFactory(PRM_SEPARATOR,"sepOutput", ""));

    parms.add(hutil::ParmFactory(PRM_STRING, "radiusattribute", "Particle Radius Attribute")
        .setDefault("pscale")
        .setTooltip("The point attribute representing the particle radius,"
                    " if the attribute does not exist, a uniform value of 1 is assumed."));

    parms.add(hutil::ParmFactory(PRM_XYZ_J, "particleradius", "Particle Radius Scale")
        .setVectorSize(3)
        .setDefault(PRMoneDefaults)
        .setRange(PRM_RANGE_RESTRICTED, 0.0, PRM_RANGE_UI, 2.0)
        .setTooltip("A multiplier on the radius of the particles to be surfaced,"
                    " if no radius attribute is supplied this becomes the particle radius."));

    parms.add(hutil::ParmFactory(PRM_SEPARATOR,"sepRadius", ""));

    parms.add(hutil::ParmFactory(PRM_TOGGLE, "useworldspaceinfluence", "Use World Space Influence Radius")
        .setDefault(PRMzeroDefaults)
        .setTooltip("If enabled, specify the influence radius explicitly in world space units, "
                    "otherwise is specified as a scale on the average (scaled by above) particle radius."));

    parms.add(hutil::ParmFactory(PRM_FLT_J, "influencescale", "Influence Radius Scale")
        .setDefault(PRMtwoDefaults)
        .setRange(PRM_RANGE_UI, 1.0, PRM_RANGE_UI, 4.0)
        .setTooltip("The distance at which particles interact is this value multiplied by the final average particle radius."
                    "Suggested values are around 2-4. "
                    "Values much larger than this can be very inefficient and give undesirable results."));

    parms.add(hutil::ParmFactory(PRM_FLT_J, "influenceradius", "Influence Radius")
        .setDefault(PRMpointOneDefaults)
        .setRange(PRM_RANGE_RESTRICTED, 0.0, PRM_RANGE_UI, 1.0)
        .setTooltip("The absolute world space value for the distance at which particles interact."
                    "Suggested values are of around 2-4x the average particle radius."
                    "Values much larger than this can be very inefficient and give undesirable results."));

    parms.add(hutil::ParmFactory(PRM_TOGGLE, "verbose", "Verbose")
        .setDefault(PRMzeroDefaults)
        .setTooltip("Output additional profiling and debug information to the terminal"));

    parms.add(hutil::ParmFactory(PRM_TOGGLE, "disablesurface", "Disable Surface")
        .setDefault(PRMzeroDefaults)
        .setTooltip("Disables the creation of the level-set to allow "
                    "you to calculate the anisotropic point distributions on the particles as "
                    "attributes. Generally used with Keep VDB Points on."));

    parms.add(hutil::ParmFactory(PRM_SEPARATOR,"sepInfluence", ""));

    // ELLIPSOID PARMS

    parms.add(hutil::ParmFactory(PRM_FLT_J, "allowedstretch", "Minimum Sphericity")
        .setDefault(0.3f)
        .setRange(PRM_RANGE_RESTRICTED, 0.01, PRM_RANGE_RESTRICTED, 1.0)
        .setTooltip("To avoid particle imprints being flattened to a disk, "
                    " limit the allowed ratio of the minimum to maximum radii of ellipsoids created (as a fraction). "
                    "A value of 0 would effectively allow a particle's imprint to be completely flattened to a disk. "
                    "A value of 1 will instead only allow spherical imprints to be created."));

    parms.add(hutil::ParmFactory(PRM_FLT_J, "averagevolume", "Volume Redistribution")
        .setDefault(0.75f)
        .setRange(PRM_RANGE_RESTRICTED, 0.0, PRM_RANGE_RESTRICTED, 1.0)
        .setTooltip("This controls the amount of global volume redistribution between ellipsoids created."
                     " A value of 0 will preserve volume locally per particle whereas 1 will preserve volume on a global scale, "
                     "allowing local variation in the size of the ellipsoid created based on the particle distribution."
                     "This can help create thinner sheets and sharper edges."));

    parms.add(hutil::ParmFactory(PRM_STRING, "inclusiongroups", "Inclusion Groups")
        .setHelpText("Specify VDB points groups to be candidates for ellipsoid computation. "
                     "Points not in these groups will be considered droplets."
                     "If empty, all points will be included.")
        .setChoiceList(&hvdb::VDBPointsGroupMenuInput1));

    parms.add(hutil::ParmFactory(PRM_FLT_J, "dropletscale", "Droplet Scale")
        .setDefault(0.75f)
        .setRange(PRM_RANGE_RESTRICTED, 0.0, PRM_RANGE_UI, 1.0)
        .setTooltip("The radius of isolated particles that have a simple spherical "
                     "imprint is calculated by scaling the initial spherical radius by "
                     "this value."));

    parms.add(hutil::ParmFactory(PRM_INT_J, "minneighbours", "Neighbour Threshold")
        .setDefault(25)
        .setRange(PRM_RANGE_RESTRICTED, 1, PRM_RANGE_UI, 200)
        .setTooltip("If particle has less neighbours than this amount, "
                     "it will be treated as an isolated dropet."));

    parms.add(hutil::ParmFactory(PRM_FLT_J, "averagepositions", "Smooth Positions")
        .setDefault(0.9f)
        .setRange(PRM_RANGE_RESTRICTED, 0.0, PRM_RANGE_RESTRICTED, 1.0)
        .setTooltip("Linearly blends between Laplacian smoothed (averaged) positions of the "
                     "particles and their original positions."
                     "Blends between original (0) and average positions (1)."));

    // transfer attributes

    parms.add(hutil::ParmFactory(PRM_HEADING, "transferheading", "Attribute Transfer"));

    hutil::ParmList attrParms;
    attrParms.add(hutil::ParmFactory(PRM_STRING, "name#", "Name")
        .setHelpText("Attribute name"));

    parms.add(hutil::ParmFactory(PRM_MULTITYPE_LIST, "numattr", "Number of Attributes")
        .setHelpText("The number of attributes to transfer.")
        .setMultiparms(attrParms)
        .setDefault(PRMzeroDefaults));

    hvdb::OpenVDBOpFactory("OVDB Points Surfacer",
        SOP_OpenVDB_Points_Surfacer::factory, parms, *table)
        .addInput("VDB Points to surface")
        .addOptionalInput("Optional VDB grid that defines the output transform. "
            "The half-band width is matched if the input grid is a level set.")
        .setDocumentation("\
#icon: COMMON/openvdb\n\
#tags: vdb\n\
\n\
\"\"\"Converts a points VDB to a levelset surface.\"\"\"\n\
\n\
@overview\n\
\n\
This node converts a points VDB to a levelset surface locally\n\
deforming spheres into ellipsoids to create smooth surfaces with sharp edges.\n\
\n\
:tip:\n\
Convert points to a points VDB using a [OpenVDB Points Convert|Node:sop/DW_OpenVDBPointsConvert] node.\n\
");
}


bool
SOP_OpenVDB_Points_Surfacer::updateParmsFlags()
{
    bool changed = false;
    const fpreal t = CHgetEvalTime();

    const bool hasRefInput = this->nInputs() == 2;

    const bool absoluteInfluence = static_cast<bool>(evalInt("useworldspaceinfluence", 0, t));

    changed |= enableParm("voxelsize", !hasRefInput);
    changed |= enableParm("referencegroup", hasRefInput);

    changed |= enableParm("influencescale", !absoluteInfluence);
    changed |= setVisibleState("influencescale", !absoluteInfluence);

    changed |= enableParm("influenceradius", absoluteInfluence);
    changed |= setVisibleState("influenceradius", absoluteInfluence);

    return changed;
}

////////////////////////////////////////


OP_Node*
SOP_OpenVDB_Points_Surfacer::factory(OP_Network* net,
    const char* name, OP_Operator* op)
{
    return new SOP_OpenVDB_Points_Surfacer(net, name, op);
}


SOP_OpenVDB_Points_Surfacer::SOP_OpenVDB_Points_Surfacer(OP_Network* net,
    const char* name, OP_Operator* op)
    : hvdb::SOP_NodeVDB(net, name, op)
{}

////////////////////////////////////////

OP_ERROR
SOP_OpenVDB_Points_Surfacer::cookVDBSop(OP_Context& context)
{
    using openvdb::points::PointDataGrid;

    try {
        hutil::ScopedInputLock lock(*this, context);
        if (duplicateSourceStealable(0, context) >= UT_ERROR_ABORT) return error();

        hvdb::Interrupter boss("VDB Point Surfacer");

        const fpreal time = context.getTime();

        const std::string surfaceName = evalStdString( "surfacevdbname", time);
        const int halfBand = evalInt("halfbandvoxels", 0, time);
        const int mode = evalInt("mode", 0, time);
        const bool keepPoints = evalInt("keep", 0, time) == 1;

        openvdb::math::Transform::Ptr sdfTransform;
        const GU_Detail* refGeo = inputGeo(1);
        if (refGeo) {
            // Get the first grid in the group's transform
            const GA_PrimitiveGroup *refGroup = matchGroup(*refGeo, evalStdString("referencegroup", time));

            hvdb::VdbPrimCIterator gridIter(refGeo, refGroup);

            if (gridIter) {
                sdfTransform = (*gridIter)->getGrid().transform().copy();
            } else {
                addError(SOP_MESSAGE, "Could not find a reference grid");
                return error();
            }
        }
        else {
            const float voxelSize = evalFloat("voxelsize", 0, time);
            sdfTransform = openvdb::math::Transform::createLinearTransform(voxelSize);
        }

        const std::string groupStr = evalStdString("group", time);
        const GA_PrimitiveGroup *group = matchGroup(*gdp, groupStr);

        const bool absoluteInfluence = static_cast<bool>(evalInt("useworldspaceinfluence", 0, time));

        const float influenceRadius = evalFloat("influenceradius", 0, time);
        const float influenceScale = evalFloat("influencescale", 0, time);

        const std::string radiusAttributeName = evalStdString("radiusattribute", time);
        const openvdb::Vec3f radiusScale = openvdb::Vec3f(
            evalFloat("particleradius", 0, time),
            evalFloat("particleradius", 1, time),
            evalFloat("particleradius", 2, time));

        const float averagePositions = evalFloat("averagepositions", 0, time);
        const float averageVolume = evalFloat("averagevolume", 0, time);

        const int neighbourThreshold = evalInt("minneighbours", 0, time);
        const float dropletScale = evalFloat("dropletscale", 0, time);

        const float allowedStretch = evalFloat("allowedstretch", 0, time);
        const bool rebuildLevelSet = static_cast<bool>(evalInt("rebuildlevelset", 0, time));
        const bool disableSurface = static_cast<bool>(evalInt("disablesurface", 0, time));

        const std::string pointGroupStr = evalStdString("vdbpointsgroups", time);
        std::vector<std::string> include, exclude;
        openvdb::points::AttributeSet::Descriptor::parseNames(include, exclude, pointGroupStr);

        // check to see if we have a point data grid
        bool hasPoints = false;
        // prims to remove if keepPoints is false
        UT_Array<GEO_Primitive*> primsToDelete;

        openvdb_houdini::VdbPrimIterator vdbIt(gdp, group);

        openvdb::GridPtrVec grids;

        // surface all point data grids
        for (; vdbIt; ++vdbIt) {

            GU_PrimVDB* vdbPrim = *vdbIt;

            // only process if grid is a PointDataGrid with leaves
            if (!openvdb::gridConstPtrCast<PointDataGrid>(vdbPrim->getConstGridPtr())) continue;
            if (!keepPoints) primsToDelete.append(*vdbIt);
            hasPoints = true;

            vdbPrim->makeGridUnique();

            PointDataGrid::Ptr points = openvdb::gridPtrCast<PointDataGrid>(vdbPrim->getGridPtr());

            const auto iter = points->constTree().cbeginLeaf();

            if (!iter) continue;
            if (boss.wasInterrupted()) break;

            const openvdb::points::AttributeSet::Descriptor&
                descriptor = iter->attributeSet().descriptor();
            const bool hasPscale(iter->hasAttribute(radiusAttributeName));

            if (hasPscale && descriptor.valueType(descriptor.find(radiusAttributeName)) !=
                std::string("float")) {
                throw std::runtime_error("Wrong attribute type for attribute " + radiusAttributeName + ", expected float");
            }

            const double averageRadius =
                (!hasPscale ? 1.0 : getAverageRadius(points->constTree(), radiusAttributeName, include, exclude));

            // search radius defined using average particle radius if !absoluteInfluence

            const double searchRadius = (absoluteInfluence ?
                influenceRadius : (influenceScale * radiusScale.x() * averageRadius));

            // determine attributes to transfer

            const int numAttrs = evalInt("numattr", 0, time);
            std::vector<std::string> transferAttributes;
            transferAttributes.reserve(numAttrs);

            for(int i = 1; i < numAttrs + 1; i++) {
                UT_String attrName;
                evalStringInst("name#", &i, attrName, 0, time);
                std::string attrNameStr = attrName.toStdString();

                // warn if attribute is missing

                if ((!attrNameStr.empty()) && descriptor.find(attrNameStr) !=
                    openvdb::points::AttributeSet::INVALID_POS) {
                    transferAttributes.emplace_back(attrNameStr);
                }
                else {
                    std::string warning = "Attribute " + attrNameStr +
                        " not available for transfer to volume";
                    addWarning(SOP_MESSAGE, warning.c_str());
                }
            }

            if (!sdfTransform->isLinear()) throw std::runtime_error("Oriented Ellipsoids option only supports Linear Transforms");

            openvdb::GridPtrVec results;

            if (mode == 0)
            {
                openvdb::points::SphereSettings<openvdb::TypeList<>, float, openvdb::points::NullFilter, hvdb::Interrupter> s;
                s.interrupter = &boss;
                s.radiusScale = radiusScale.x();
                s.radius = radiusAttributeName;
                s.halfband = halfBand;

                results = openvdb::points::rasterizeSdf(*points, s);
                results[0]->setName(surfaceName);
            }
            else if (mode == 1)
            {
                openvdb::points::SmoothSphereSettings<openvdb::TypeList<>, float, openvdb::points::NullFilter, hvdb::Interrupter> s;
                s.interrupter = &boss;
                s.radiusScale = radiusScale.x();
                s.radius = radiusAttributeName;
                s.searchRadius = searchRadius;
                s.halfband = halfBand;

                results = openvdb::points::rasterizeSdf(*points, s);
                results[0]->setName(surfaceName);
            }
            else if (mode == 2)
            {
                // get inclusion groups - only points in these groups will have ellipsoids
                // calculated for them.  If empty, all points will be included.

                const std::string ellipsoidGroups = evalStdString("inclusiongroups", time);
                std::vector<std::string> includeEllipsoidInclusionGroups;
                std::vector<std::string> excludeEllipsoidInclusionGroups;

                openvdb::points::AttributeSet::Descriptor::parseNames(includeEllipsoidInclusionGroups,
                    excludeEllipsoidInclusionGroups, ellipsoidGroups);
                setInclusionGroup(points->tree(), includeEllipsoidInclusionGroups, excludeEllipsoidInclusionGroups, "__calc_ellipsoid");

                //Calculate ellipsoids from local neighbourhood
                // @TODO: update to take particle radius attribute
                boss.start("Calculating ellipsoid deformations from point distribution");

                openvdb::points::PcaAttributes a;
                openvdb::points::PcaSettings s;
                s.searchRadius = searchRadius;
                s.neighbourThreshold = neighbourThreshold;
                s.allowedAnisotropyRatio = allowedStretch;
                s.averagePositions = averagePositions;
                s.nonAnisotropicStretch = dropletScale;

                std::cerr << "s.searchRadius " << s.searchRadius << std::endl;
                std::cerr << "s.neighbourThreshold " << s.neighbourThreshold << std::endl;
                std::cerr << "s.allowedAnisotropyRatio " << s.allowedAnisotropyRatio << std::endl;
                std::cerr << "s.averagePositions " << s.averagePositions << std::endl;
                std::cerr << "s.nonAnisotropicStretch " << s.nonAnisotropicStretch << std::endl;

                openvdb::points::pca<PointDataGrid, openvdb::points::NullFilter, hvdb::Interrupter>(*points, s, a, &boss);

                if (!disableSurface) {
                    if (boss.wasInterrupted()) return error();
                    boss.start("Stamping ellipsoids into surface");

                    openvdb::points::EllipsoidSettings<openvdb::TypeList<>, openvdb::Vec3f, openvdb::points::NullFilter, hvdb::Interrupter> es;
                    es.interrupter = &boss;
                    es.radiusScale = radiusScale;
                    es.halfband = halfBand;
                    es.transform = sdfTransform;

                    es.radius = a.stretch;
                    es.rotation = a.rotation;
                    if (s.averagePositions > 0)
                        es.pws = a.positionWS;

                    std::cerr << "es.radiusScale " << es.radiusScale << std::endl;
                    std::cerr << "es.radius " << es.radius << std::endl;

                    results = openvdb::points::rasterizeSdf(*points, es);
                    results[0]->setName(surfaceName);
                }
            }

            if (!results.empty())
            {
                if (rebuildLevelSet) {
                    openvdb::FloatGrid::Ptr sdf = openvdb::StaticPtrCast<openvdb::FloatGrid>(results.front());
                    results[0] = openvdb::tools::levelSetRebuild(*sdf, 0, halfBand, halfBand);
                }
                grids.insert(grids.end(), results.begin(), results.end());
            }
        }

        for (const auto& grid : grids) {
            hvdb::createVdbPrimitive(*gdp, grid);
        }

        // if no point data grids found throw warning
        if (!hasPoints) {
            addWarning(SOP_MESSAGE, "No VDB Points primitives found.");
            return error();
        }

        if (!primsToDelete.isEmpty()) {
            gdp->deletePrimitives(primsToDelete, true);
        }

    } catch (std::exception& e) {
        addError(SOP_MESSAGE, e.what());
    }

    return error();
}
