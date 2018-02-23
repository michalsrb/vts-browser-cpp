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

#include "map.hpp"

namespace vts
{

using vtslibs::registry::View;
using vtslibs::registry::BoundLayer;

BoundParamInfo::BoundParamInfo(const View::BoundLayerParams &params)
    : View::BoundLayerParams(params), orig(0), vars(0),
      bound(nullptr), depth(0), watertight(true), transparent(false)
{}

mat3f BoundParamInfo::uvMatrix() const
{
    int dep = depth;
    if (dep == 0)
        return identityMatrix3().cast<float>();
    double scale = 1.0 / (1 << dep);
    double tx = scale * (orig.localId.x
                         - ((orig.localId.x >> dep) << dep));
    double ty = scale * (orig.localId.y
                         - ((orig.localId.y >> dep) << dep));
    ty = 1 - scale - ty;
    mat3f m;
    m << scale, 0, tx,
            0, scale, ty,
            0, 0, 1;
    return m;
}

Validity BoundParamInfo::prepare(const NodeInfo &nodeInfo, MapImpl *impl,
                             uint32 subMeshIndex, double priority)
{
    bound = impl->mapConfig->getBoundInfo(id);
    if (!bound)
        return Validity::Indeterminate;

    // check lodRange and tileRange
    {
        TileId t = nodeInfo.nodeId();
        int m = bound->lodRange.min;
        if (t.lod < m)
            return Validity::Invalid;
        t.x >>= t.lod - m;
        t.y >>= t.lod - m;
        if (t.x < bound->tileRange.ll[0] || t.x > bound->tileRange.ur[0])
            return Validity::Invalid;
        if (t.y < bound->tileRange.ll[1] || t.y > bound->tileRange.ur[1])
            return Validity::Invalid;
    }

    orig = vars = UrlTemplate::Vars(nodeInfo.nodeId(), local(nodeInfo),
                                    subMeshIndex);

    depth = std::max(nodeInfo.nodeId().lod - bound->lodRange.max, 0);
    if (depth > 0)
    {
        vars.tileId.lod -= depth;
        vars.tileId.x >>= depth;
        vars.tileId.y >>= depth;
        vars.localId.lod -= depth;
        vars.localId.x >>= depth;
        vars.localId.y >>= depth;
    }

    // bound meta node
    if (bound->metaUrl)
    {
        UrlTemplate::Vars v(vars);
        v.tileId.x &= ~255;
        v.tileId.y &= ~255;
        v.localId.x &= ~255;
        v.localId.y &= ~255;
        std::string boundName = bound->urlMeta(v);
        std::shared_ptr<BoundMetaTile> bmt = impl->getBoundMetaTile(boundName);
        bmt->updatePriority(priority);
        switch (impl->getResourceValidity(bmt))
        {
        case Validity::Indeterminate:
            return Validity::Indeterminate;
        case Validity::Invalid:
            return Validity::Invalid;
        case Validity::Valid:
            break;
        }
        uint8 f = bmt->flags[(vars.tileId.y & 255) * 256
                + (vars.tileId.x & 255)];
        if ((f & BoundLayer::MetaFlags::available)
                != BoundLayer::MetaFlags::available)
            return Validity::Invalid;
        watertight = (f & BoundLayer::MetaFlags::watertight)
                == BoundLayer::MetaFlags::watertight;
    }

    transparent = bound->isTransparent || (!!alpha && *alpha < 1);

    return Validity::Valid;
}

DrawTask::DrawTask() :
    color{0,0,0,1}, uvClip{-1,-1,2,2}, center{0,0,0},
    externalUv(false), flatShading(false)
{
    for (int i = 0; i < 16; i++)
        mv[i] = i % 4 == i / 4 ? 1 : 0;
    for (int i = 0; i < 9; i++)
        uvm[i] = i % 3 == i / 3 ? 1 : 0;
}

DrawTask::DrawTask(const RenderTask &r, const MapImpl *m) :
    mesh(r.mesh->info.userData),
    uvClip{0,0,1,1},
    externalUv(r.externalUv),
    flatShading(r.flatShading || m->options.debugFlatShading)
{
    assert(r.ready());
    if (r.textureColor)
        texColor = r.textureColor->info.userData;
    if (r.textureMask)
        texMask = r.textureMask->info.userData;
    for (int i = 0; i < 4; i++)
        this->color[i] = r.color[i];
    mat4f mv = (m->renderer.viewRender * r.model).cast<float>();
    for (int i = 0; i < 16; i++)
        this->mv[i] = mv(i);
    for (int i = 0; i < 9; i++)
        this->uvm[i] = r.uvm(i);
    vec3f c = vec4to3(r.model * vec4(0,0,0,1)).cast<float>();
    vecToRaw(c, center);
}

DrawTask::DrawTask(const RenderTask &r, const float *uvClip, const MapImpl *m)
 : DrawTask(r, m)
{
    for (int i = 0; i < 4; i++)
        this->uvClip[i] = uvClip[i];

    // debug
    /*
    if (uvClip[0] != -1)
    {
        for (int i = 0; i < 3; i++)
            color[i] *= 0.4;
    }
    */
}

MapDraws::MapDraws()
{
    memset(&camera, 0, sizeof(camera));
}

void MapDraws::clear()
{
    grids.clear();
    opaque.clear();
    transparent.clear();
    Infographic.clear();
}

void MapDraws::sortOpaqueFrontToBack()
{
    vec3 e = rawToVec3(camera.eye);
    std::sort(opaque.begin(), opaque.end(), [e](const DrawTask &a,
              const DrawTask &b) {
        vec3 va = rawToVec3(a.center).cast<double>() - e;
        vec3 vb = rawToVec3(b.center).cast<double>() - e;
        return dot(va, va) < dot(vb, vb);
    });
}

RenderTask::RenderTask() : model(identityMatrix4()),
    uvm(identityMatrix3().cast<float>()),
    color(1,1,1,1), externalUv(false), flatShading(false)
{}

bool RenderTask::ready() const
{
    if (meshAgg && !*meshAgg)
        return false;
    if (!*mesh)
        return false;
    if (textureColor && !*textureColor)
        return false;
    if (textureMask && !*textureMask)
        return false;
    return true;
}

ExternalBoundLayer::ExternalBoundLayer(MapImpl *map, const std::string &name)
    : Resource(map, name, FetchTask::ResourceType::BoundLayerConfig)
{
    priority = std::numeric_limits<float>::infinity();
}

void ExternalBoundLayer::load()
{
    detail::Wrapper w(reply.content);
    *(vtslibs::registry::BoundLayer*)this
            = vtslibs::registry::loadBoundLayer(w, name);
}

TilesetMapping::TilesetMapping(MapImpl *map, const std::string &name) :
    Resource(map, name, FetchTask::ResourceType::TilesetMappingConfig)
{
    priority = std::numeric_limits<float>::infinity();
}

void TilesetMapping::load()
{
    LOG(info2) << "Loading tileset mapping <" << name << ">";
    dataRaw = vtslibs::vts::deserializeTsMap(std::string(reply.content.data(),
                                                         reply.content.size()));
}

void TilesetMapping::update(const std::vector<std::string> &vsId)
{
    surfaceStack.clear();
    surfaceStack.reserve(dataRaw.size() + 1);
    // the sourceReference in metanodes is one-based
    surfaceStack.push_back(MapConfig::SurfaceStackItem());
    for (auto &it : dataRaw)
    {
        if (it.size() == 1)
        { // surface
            MapConfig::SurfaceStackItem i;
            i.surface = std::make_shared<MapConfig::SurfaceInfo>(
                    *map->mapConfig->findSurface(vsId[it[0]]),
                    map->mapConfig->name);
            i.surface->name.push_back(vsId[it[0]]);
            surfaceStack.push_back(i);
        }
        else
        { // glue
            std::vector<std::string> id;
            id.reserve(it.size());
            for (auto &it2 : it)
                id.push_back(vsId[it2]);
            MapConfig::SurfaceStackItem i;
            i.surface = std::make_shared<MapConfig::SurfaceInfo>(
                    *map->mapConfig->findGlue(id),
                    map->mapConfig->name);
            i.surface->name = id;
            surfaceStack.push_back(i);
        }
    }
    MapConfig::colorizeSurfaceStack(surfaceStack);
}

} // namespace vts
