#ifndef WEBRUNTIME_H
#define WEBRUNTIME_H

#include <string>

namespace WebRuntime {

int defaultVideoWidth();
int defaultVideoHeight();
void reportMatchStats(const std::string& phase, const std::string& matchID, const std::string& payload);
void yieldToBrowser();
void markGameReady();
void syncPersistentFiles();

}

#endif
