#pragma once

#include "main.hpp"
#include "uri.hh"
#include "libcurl/shared/curl.h"
#include "libcurl/shared/easy.h"

#include "BeatSaverRegionManager.hpp"

#include <future>

namespace songdetails {

    static void RegionLookup(bool force = false) {
            if(didTheThing && !force)
                return;

            didTheThing = true;
            GetJSONAsync(detailsDownloadUrl + "225eb", [](long status, bool error, rapidjson::Document const& result){
                if (status == 200) {
                    std::string joe = result["versions"].GetArray()[0]["coverURL"].GetString();
                    if(joe.length() > 0) {
                        uri u(joe);

                        coverDownloadUrl = previewDownloadUrl = fmt::format("{}://{}", u.get_scheme(), u.get_host());
                        return;
                    }
                }
            });
    }
}
