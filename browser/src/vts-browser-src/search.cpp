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

#include <jsoncpp/json.hpp>

#include "map.hpp"

namespace vts
{

namespace
{

std::string generateSearchUrl(MapImpl *impl, const std::string &query)
{
    std::string url = impl->options.searchUrl;
    static const std::string rep = "{query}";
    auto s = url.find(rep);
    if (s != std::string::npos)
        url.replace(s, rep.length(), query);
    return url;
}

void latlonToNav(MapImpl *map, double point[3])
{
    (void)map;
    (void)point;
    // todo
}

// both points are in navigation srs
double distance(MapImpl *map, const double a[], const double b[],
                double def = std::numeric_limits<double>::quiet_NaN())
{
    for (int i = 0; i < 3; i++)
        if (a[i] != a[i] || b[i] != b[i])
            return def;
    vec3 va(a[0], a[1], a[2]);
    vec3 vb(b[0], b[1], b[2]);
    switch (map->mapConfig->srs.get(
                map->mapConfig->referenceFrame.model.navigationSrs).type)
    {
    case vtslibs::registry::Srs::Type::cartesian:
    case vtslibs::registry::Srs::Type::projected:
        return length(vec3(vb - va));
    case vtslibs::registry::Srs::Type::geographic:
        return map->mapConfig->convertor->navGeodesicDistance(va, vb);
    }
    return def;
}

bool populated(const SearchItem &a)
{
    return a.type == "hamlet"
        || a.type == "village"
        || a.type == "town"
        || a.type == "city";
}

void filterSearchResults(MapImpl *map, const std::shared_ptr<SearchTask> &task)
{
    // dedupe
    if (task->results.size() > 1)
    {
        std::stable_sort(task->results.begin(), task->results.end(),
            [](const SearchItem &a, const SearchItem &b){
            if (a.displayName == b.displayName)
                return populated(a) > populated(b);
            return false;
        });
        task->results.erase(
                    std::unique(task->results.begin(), task->results.end(),
                                [](const SearchItem &a, const SearchItem &b){
            return a.displayName == b.displayName;
        }), task->results.end());
    }
    
    // filter results, that are close to each other
    if (task->results.size() > 1)
    {
        task->results.erase(
                    std::unique(task->results.begin(), task->results.end(),
                                [map](SearchItem &a, SearchItem &b){
            return distance(map, a.position, b.position) < 1e4;
        }), task->results.end());
    }
    
    // update some fields
    for (SearchItem &it : task->results)
    {
        // title
        {
            auto s = it.displayName.find(",");
            if (s != std::string::npos)
                it.title = it.displayName.substr(0, s);
            else
                it.title = it.displayName;
        }
        // region
        {
            if (!it.county.empty())
                it.region = it.county;
            else if (!it.stateDistrict.empty())
                it.region = it.stateDistrict;
            else
                it.region = it.state;
            auto s = it.region.find(" - ");
            if (s != std::string::npos)
                it.region = it.region.substr(0, s);
        }
        // street name queries
        if (it.title == it.houseNumber && !it.road.empty())
            it.title = it.road;
    }
    
    // reshake hits by distance
    std::stable_sort(task->results.begin(), task->results.end(),
                     [](const SearchItem &a, const SearchItem &b){
        return a.importance < 0.4 && b.importance < 0.4
                && std::abs(a.importance - b.importance) < 0.06
                && a.distance < b.distance;
    });
}

double vtod(Json::Value &v)
{
    if (v.type() == Json::ValueType::realValue)
        return v.asDouble();
    double r = std::numeric_limits<double>::quiet_NaN();
    sscanf(v.asString().c_str(), "%lf", &r);
    return r;
}

void parseSearchResults(MapImpl *map, const std::shared_ptr<SearchTask> &task)
{
    assert(!task->done);
    try
    {
        Json::Value root;
        {
            Json::Reader r;
            detail::Wrapper w(task->impl->data);
            if (!r.parse(w, root, false))
                LOGTHROW(err2, std::runtime_error)
                        << "failed to parse search result json, url: '"
                        << task->impl->fetch->name << "', error: '"
                        << r.getFormattedErrorMessages() << "'";
        }
        for (Json::Value &it : root)
        {
            SearchItem t;
            t.displayName = it["display_name"].asString();
            t.type = it["type"].asString();
            Json::Value addr = it["address"];
            if (!addr.empty())
            {
                t.houseNumber = addr["house_number"].asString();
                t.road = addr["road"].asString();
                t.city = addr["city"].asString();
                t.county = addr["county"].asString();
                t.state = addr["state"].asString();
                t.stateDistrict = addr["state_district"].asString();
                t.country = addr["country"].asString();
                t.countryCode = addr["country_code"].asString();
            }
            t.position[0] = vtod(it["lon"]);
            t.position[1] = vtod(it["lat"]);
            t.position[2] = 0;
            latlonToNav(map, t.position);
            t.distance = distance(map, task->position, t.position);
            Json::Value bj = it["boundingbox"];
            if (bj.size() == 4)
            {
                double r[4];
                int i = 0;
                for (auto it : bj)
                    r[i++] = vtod(it);
                double bbs[4][3] = {
                    { r[2], r[0], 0 },
                    { r[2], r[1], 0 },
                    { r[3], r[0], 0 },
                    { r[3], r[1], 0 }
                };
                t.radius = 3333;
                for (int i = 0; i < 4; i++)
                {
                    latlonToNav(map, bbs[i]);
                    t.radius = std::max(t.radius,
                                        distance(map, t.position, bbs[i]));
                }
            }
            t.importance = vtod(it["importance"]);
            task->results.push_back(t);
        }
        if (map->options.searchResultsFilter)
            filterSearchResults(map, task);
    }
    catch (const Json::Exception &e)
    {
        LOG(err3) << "failed to process search results, url: '"
                  << task->impl->fetch->name << "', query: '"
                  << task->query << "', error: '"
                  << e.what() << "'";
    }
    catch (...)
    {
        LOG(err3) << "failed to process search results, url: '"
                  << task->impl->fetch->name << "', query: '"
                  << task->query << "'";
    }
}

} // namespace

void SearchTaskImpl::load()
{
    data = std::move(fetch->contentData);
}

SearchItem::SearchItem() :
    position{ std::numeric_limits<double>::quiet_NaN(),
              std::numeric_limits<double>::quiet_NaN(),
              std::numeric_limits<double>::quiet_NaN()},
    radius(std::numeric_limits<double>::quiet_NaN()),
    distance(std::numeric_limits<double>::quiet_NaN()),
    importance(-1)
{}

SearchTask::SearchTask(const std::string &query, const double point[3]) :
    query(query), position{point[0], point[1], point[2]}, done(false)
{}

SearchTask::~SearchTask()
{}

std::shared_ptr<SearchTask> MapImpl::search(const std::string &query,
                                            const double point[3])
{
    auto t = std::make_shared<SearchTask>(query, point);
    t->impl = getSearchImpl(generateSearchUrl(this, query));
    t->impl->fetch->queryHeaders["Accept-Language"] = "en-US,en";
    resources.searchTasks.push_back(t);
    return t;
}

void MapImpl::updateSearch()
{
    auto it = resources.searchTasks.begin();
    while (it != resources.searchTasks.end())
    {
        std::shared_ptr<SearchTask> t = it->lock();
        if (t)
        {
            switch (getResourceValidity(t->impl))
            {
            case Validity::Indeterminate:
                touchResource(t->impl);
                it++;
                continue;
            case Validity::Invalid:
                break;
            case Validity::Valid:
                parseSearchResults(this, t);
            }
            t->done = true;
        }
        it = resources.searchTasks.erase(it);
    }
}

} // namespace vts
