/**
 * Copyright (c) 2017 Melown Technologies SE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * *  Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <vts-libs/registry/json.hpp>
#include <boost/algorithm/string.hpp>

#include "include/vts-browser/map.hpp"
#include "include/vts-browser/view.hpp"
#include "map.hpp"

namespace vts
{

namespace
{

inline const std::string srsConvert(MapConfig *config, Srs srs)
{
    switch(srs)
    {
    case Srs::Navigation:
        return config->referenceFrame.model.navigationSrs;
    case Srs::Physical:
        return config->referenceFrame.model.physicalSrs;
    case Srs::Public:
        return config->referenceFrame.model.publicSrs;
    default:
        LOGTHROW(fatal, std::invalid_argument) << "Invalid srs";
    }
    throw;
}

void getMapViewBoundLayers(
        const vtslibs::registry::View::BoundLayerParams::list &in,
        MapView::BoundLayerInfo::Map &out)
{
    out.clear();
    for (auto &&it : in)
    {
        MapView::BoundLayerInfo b(it.id);
        b.alpha = it.alpha ? *it.alpha : 1;
        out.push_back(b);
    }
}

void setMapViewBoundLayers(
        const MapView::BoundLayerInfo::Map &in,
        vtslibs::registry::View::BoundLayerParams::list &out
        )
{
    out.clear();
    for (auto &&it : in)
    {
        vtslibs::registry::View::BoundLayerParams b;
        b.id = it.id;
        if (it.alpha < 1 - 1e-7)
            b.alpha = it.alpha;
        out.push_back(b);
    }
}

const MapView getMapView(const vtslibs::registry::View &view)
{
    MapView value;
    value.description = view.description ? *view.description : "";
    for (auto &&it : view.surfaces)
    {
        MapView::SurfaceInfo &s = value.surfaces[it.first];
        getMapViewBoundLayers(it.second, s.boundLayers);
    }
    for (auto &&it : view.freeLayers)
    {
        MapView::FreeLayerInfo &f = value.freeLayers[it.first];
        f.style = it.second.style ? *it.second.style : "";
        getMapViewBoundLayers(it.second.boundLayers, f.boundLayers);
    }
    return std::move(value);
}

const vtslibs::registry::View setMapView(const MapView &value)
{
    vtslibs::registry::View view;
    if (!value.description.empty())
        view.description = value.description;
    for (auto &&it : value.surfaces)
    {
        vtslibs::registry::View::BoundLayerParams::list &b
                = view.surfaces[it.first];
        setMapViewBoundLayers(it.second.boundLayers, b);
    }
    for (auto &&it : value.freeLayers)
    {
        vtslibs::registry::View::FreeLayerParams &f
                = view.freeLayers[it.first];
        if (!it.second.style.empty())
            f.style = it.second.style;
        setMapViewBoundLayers(it.second.boundLayers, f.boundLayers);
    }
    return std::move(view);
}

} // namespace

MapView::BoundLayerInfo::BoundLayerInfo() : alpha(1)
{}

MapView::BoundLayerInfo::BoundLayerInfo(const std::string &id) :
    id(id), alpha(1)
{}

MapCreateOptions::MapCreateOptions(const std::string &clientId) :
    clientId(clientId), disableCache(false)
{}

Map::Map(const MapCreateOptions &options)
{
    LOG(info4) << "Creating map";
    impl = std::shared_ptr<MapImpl>(new MapImpl(this, options));
}

Map::~Map()
{
    LOG(info4) << "Destroying map";
}

void Map::dataInitialize(const std::shared_ptr<Fetcher> &fetcher)
{
    dbglog::thread_id("data");
    impl->resourceDataInitialize(fetcher);
}

bool Map::dataTick()
{
    return impl->resourceDataTick();
}

void Map::dataFinalize()
{
    impl->resourceDataFinalize();
}

void Map::renderInitialize()
{
    dbglog::thread_id("render");
    impl->resourceRenderInitialize();
    impl->renderInitialize();
}

void Map::renderTick(uint32 width, uint32 height)
{
    impl->statistics.resetFrame();
    impl->resourceRenderTick();
    impl->renderTick(width, height);
}

void Map::renderFinalize()
{
    impl->resourceRenderFinalize();
    impl->renderFinalize();
}

void Map::setMapConfigPath(const std::string &mapConfigPath,
                           const std::string &authPath)
{
    impl->setMapConfigPath(mapConfigPath, authPath);
}

const std::string &Map::getMapConfigPath() const
{
    return impl->mapConfigPath;
}

void Map::purgeTraverseCache()
{
    if (!isMapConfigReady())
        return;
    impl->purgeTraverseCache();
}

bool Map::isMapConfigReady() const
{
    return impl->mapConfig && *impl->mapConfig;
}

bool Map::isMapRenderComplete() const
{
    if (!isMapConfigReady())
        return false;
    return !impl->draws.draws.empty()
            && impl->statistics.currentNodeUpdates == 0
            && impl->statistics.currentResourcePreparing == 0;
}

double Map::getMapRenderProgress() const
{
    if (!isMapConfigReady())
        return 0;
    if (isMapRenderComplete())
        return 1;
    return 0.5; // todo some better heuristic
}

void Map::pan(const double value[3])
{
    if (!isMapConfigReady())
        return;
    impl->pan(vec3(value[0], value[1], value[2]));
}

void Map::pan(const double (&value)[3])
{
    pan(&value[0]);
}

void Map::rotate(const double value[3])
{
    if (!isMapConfigReady())
        return;
    impl->rotate(vec3(value[0], value[1], value[2])); 
}

void Map::rotate(const double (&value)[3])
{
    rotate(&value[0]);
}

void Map::zoom(double value)
{
    if (!isMapConfigReady())
        return;
    impl->zoom(value);
}

MapCallbacks &Map::callbacks()
{
    return impl->callbacks;
}

MapStatistics &Map::statistics()
{
    return impl->statistics;
}

MapOptions &Map::options()
{
    return impl->options;
}

MapDraws &Map::draws()
{
    return impl->draws;
}

MapCredits &Map::credits()
{
    return impl->credits;
}

void Map::setPositionSubjective(bool subjective, bool convert)
{
    if (!isMapConfigReady() || subjective == getPositionSubjective())
        return;
    if (convert)
        impl->convertPositionSubjObj();
    impl->mapConfig->position.type = subjective
            ? vtslibs::registry::Position::Type::subjective
            : vtslibs::registry::Position::Type::objective;
}

bool Map::getPositionSubjective() const
{
    if (!isMapConfigReady())
        return false;
    return impl->mapConfig->position.type
            == vtslibs::registry::Position::Type::subjective;
}

void Map::setPositionPoint(const double point[3])
{
    if (!isMapConfigReady())
        return;
    if (impl->mapConfig->srs.get(
                impl->mapConfig->referenceFrame.model.navigationSrs).type
                   == vtslibs::registry::Srs::Type::geographic)
    {
        assert(point[0] >= -180 && point[0] <= 180);
        assert(point[1] >= -90 && point[1] <= 90);
    }
    impl->mapConfig->position.position
            = math::Point3(point[0], point[1], point[2]);
    impl->navigation.inertiaMotion = vec3(0,0,0);
}

void Map::setPositionPoint(const double (&point)[3])
{
    setPositionPoint(&point[0]);
}

void Map::getPositionPoint(double point[3]) const
{
    if (!isMapConfigReady())
    {
        for (int i = 0; i < 3; i++)
            point[i] = 0;
        return;
    }
    auto p = impl->mapConfig->position.position;
    for (int i = 0; i < 3; i++)
        point[i] = p[i];
}

void Map::setPositionRotation(const double point[3])
{
    if (!impl->mapConfig || !*impl->mapConfig)
        return;
    assert(point[0] == point[0]);
    assert(point[1] == point[1]);
    assert(point[2] == point[2]);
    impl->mapConfig->position.orientation
            = math::Point3(point[0], point[1], point[2]);
    impl->navigation.inertiaRotation = vec3(0,0,0);
}

void Map::setPositionRotation(const double (&point)[3])
{
    setPositionRotation(&point[0]);
}

void Map::getPositionRotation(double point[3]) const
{
    if (!isMapConfigReady())
    {
        for (int i = 0; i < 3; i++)
            point[i] = 0;
        return;
    }
    auto p = impl->mapConfig->position.orientation;
    for (int i = 0; i < 3; i++)
        point[i] = p[i];
}

void Map::setPositionViewExtent(double viewExtent)
{
    if (!isMapConfigReady())
        return;
    assert(viewExtent == viewExtent);
    impl->mapConfig->position.verticalExtent = viewExtent;
    impl->navigation.inertiaViewExtent = 0;
}

double Map::getPositionViewExtent() const
{
    if (!isMapConfigReady())
        return 0;
    return impl->mapConfig->position.verticalExtent;
}

void Map::setPositionFov(double fov)
{
    if (!isMapConfigReady())
        return;
    assert(fov > 0 && fov < 180);
    impl->mapConfig->position.verticalFov = fov;
}

double Map::getPositionFov() const
{
    if (!isMapConfigReady())
        return 0;
    return impl->mapConfig->position.verticalFov;
}

std::string Map::getPositionJson() const
{
    if (!isMapConfigReady())
        return "";
    return Json::FastWriter().write(
                vtslibs::registry::asJson(impl->mapConfig->position));
}

void Map::setPositionJson(const std::string &position)
{
    if (!isMapConfigReady())
        return;
    Json::Value val;
    if (!Json::Reader().parse(position, val))
        LOGTHROW(err2, std::runtime_error) << "invalid position json";
    impl->mapConfig->position = vtslibs::registry::positionFromJson(val);
}

void Map::resetPositionAltitude()
{
    if (!isMapConfigReady())
        return;
    impl->resetPositionAltitude(0);
}

void Map::resetPositionRotation(bool immediate)
{
    if (!isMapConfigReady())
        return;
    impl->resetPositionRotation(immediate);
}

void Map::resetNavigationMode()
{
    if (!isMapConfigReady())
        return;
    impl->resetNavigationMode();
}

void Map::setAutoMotion(const double value[3])
{
    if (!isMapConfigReady())
        return;
    for (int i = 0; i < 3; i++)
        impl->navigation.autoMotion[i] = value[i];
}

void Map::setAutoMotion(const double (&value)[3])
{
    setAutoMotion(&value[0]);
}

void Map::getAutoMotion(double value[3]) const
{
    if (!isMapConfigReady())
    {
        for (int i = 0; i < 3; i++)
            value[i] = 0;
        return;
    }
    for (int i = 0; i < 3; i++)
        value[i] = impl->navigation.autoMotion(i);
}

void Map::setAutoRotation(const double value[3])
{
    if (!isMapConfigReady())
        return;
    for (int i = 0; i < 3; i++)
        impl->navigation.autoRotation[i] = value[i];
}

void Map::setAutoRotation(const double (&value)[3])
{
    setAutoRotation(&value[0]);
}

void Map::getAutoRotation(double value[3]) const
{
    if (!isMapConfigReady())
    {
        for (int i = 0; i < 3; i++)
            value[i] = 0;
        return;
    }
    for (int i = 0; i < 3; i++)
        value[i] = impl->navigation.autoRotation(i);
}

void Map::convert(const double pointFrom[3], double pointTo[3],
                  Srs srsFrom, Srs srsTo) const
{
    if (!isMapConfigReady())
        return;
    vec3 a(pointFrom[0], pointFrom[1], pointFrom[2]);
    a = impl->mapConfig->convertor->convert(a,
                    srsConvert(impl->mapConfig.get(), srsFrom),
                    srsConvert(impl->mapConfig.get(), srsTo));
    for (int i = 0; i < 3; i++)
        pointTo[i] = a(i);
}

const std::vector<std::string> Map::getResourceSurfaces() const
{
    if (!isMapConfigReady())
        return {};
    std::vector<std::string> names;
    names.reserve(impl->mapConfig->surfaces.size());
    for (auto &&it : impl->mapConfig->surfaces)
        names.push_back(it.id);
    return std::move(names);
}

const std::vector<std::string> Map::getResourceBoundLayers() const
{
    if (!isMapConfigReady())
        return {};
    std::vector<std::string> names;
    for (auto &&it : impl->mapConfig->boundLayers)
        names.push_back(it.id);
    return std::move(names);
}

const std::vector<std::string> Map::getResourceFreeLayers() const
{
    if (!isMapConfigReady())
        return {};
    std::vector<std::string> names;
    for (auto &&it : impl->mapConfig->freeLayers)
        names.push_back(it.first);
    return std::move(names);
}

std::vector<std::string> Map::getViewNames() const
{
    if (!isMapConfigReady())
        return {};
    std::vector<std::string> names;
    names.reserve(impl->mapConfig->namedViews.size());
    for (auto &&it : impl->mapConfig->namedViews)
        names.push_back(it.first);
    return std::move(names);
}

std::string Map::getViewCurrent() const
{
    if (!isMapConfigReady())
        return "";
    return impl->mapConfigView;
}

void Map::setViewCurrent(const std::string &name)
{
    if (!isMapConfigReady())
        return;
    auto it = impl->mapConfig->namedViews.find(name);
    if (it == impl->mapConfig->namedViews.end())
        LOGTHROW(err2, std::runtime_error) << "invalid mapconfig view name";
    impl->mapConfig->view = it->second;
    impl->purgeTraverseCache();
    impl->mapConfigView = name;
}

void Map::getViewData(const std::string &name, MapView &view) const
{
    if (!isMapConfigReady())
        return;
    if (name == "")
    {
        view = getMapView(impl->mapConfig->view);
        return;
    }
    auto it = impl->mapConfig->namedViews.find(name);
    if (it == impl->mapConfig->namedViews.end())
        LOGTHROW(err2, std::runtime_error) << "invalid mapconfig view name";
    view = getMapView(it->second);
}

void Map::setViewData(const std::string &name, const MapView &view)
{
    if (!isMapConfigReady())
        return;
    if (name == "")
    {
        impl->mapConfig->view = setMapView(view);
        impl->purgeTraverseCache();
    }
    else
        impl->mapConfig->namedViews[name] = setMapView(view);
}

std::string Map::getViewJson(const std::string &name) const
{
    if (!isMapConfigReady())
        return "";
    if (name == "")
        return Json::FastWriter().write(vtslibs::registry::asJson(
                impl->mapConfig->view, impl->mapConfig->boundLayers));
    auto it = impl->mapConfig->namedViews.find(name);
    if (it == impl->mapConfig->namedViews.end())
        LOGTHROW(err2, std::runtime_error) << "invalid mapconfig view name";
    return Json::FastWriter().write(vtslibs::registry::asJson(it->second,
                        impl->mapConfig->boundLayers));
}

void Map::setViewJson(const std::string &name,
                                const std::string &view)
{
    if (!isMapConfigReady())
        return;
    Json::Value val;
    if (!Json::Reader().parse(view, val))
        LOGTHROW(err2, std::runtime_error) << "invalid view json";
    if (name == "")
    {
        impl->mapConfig->view = vtslibs::registry::viewFromJson(val);
        impl->purgeTraverseCache();
    }
    else
    {
        impl->mapConfig->namedViews[name]
                = vtslibs::registry::viewFromJson(val);
    }
}

std::shared_ptr<SearchTask> Map::search(const std::string &query)
{
    if (!isMapConfigReady())
        return {};
    return search(query, impl->mapConfig->position.position.data().begin());
}

std::shared_ptr<SearchTask> Map::search(const std::string &query,
                                        const double point[3])
{
    if (!isMapConfigReady())
        return {};
    return impl->search(query, point);
}

std::shared_ptr<SearchTask> Map::search(const std::string &query,
                                        const double (&point)[3])
{
    return search(query, &point[0]);
}

void Map::printDebugInfo()
{
    impl->printDebugInfo();
}

MapImpl::MapImpl(Map *map, const MapCreateOptions &options) :
    map(map), clientId(options.clientId), initialized(false),
    resources(options)
{}

void MapImpl::printDebugInfo()
{
    LOG(info3) << "-----DEBUG INFO-----";
    LOG(info3) << "mapconfig path: " << mapConfigPath;
    if (!mapConfig)
    {
        LOG(info3) << "missing mapconfig";
        return;
    }
    if (!*mapConfig)
    {
        LOG(info3) << "mapconfig not ready";
        return;
    }
    LOG(info3) << "Position: " << map->getPositionJson();
    LOG(info3) << "Named views: " << boost::join(map->getViewNames(), ", ");
    LOG(info3) << "Current view name: " << map->getViewCurrent();
    LOG(info3) << "Current view data: " << map->getViewJson("");
    mapConfig->printSurfaceStack();
}

} // namespace vts
