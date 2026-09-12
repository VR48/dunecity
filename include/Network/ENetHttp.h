/*
 *  This file is part of Dune Legacy.
 *
 *  Dune Legacy is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  Dune Legacy is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Dune Legacy.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ENETHTTP_H
#define ENETHTTP_H

#include <cstdint>
#include <functional>
#include <string>
#include <map>

#define PORT_HTTP   80

std::string getDomainFromURL(const std::string& url);

std::string getFilePathFromURL(const std::string& url);

int getPortFromURL(const std::string& url);

std::string percentEncode(const std::string & s);

std::string loadFromHttp(const std::string& url,
                         const std::map<std::string, std::string>& parameters = std::map<std::string, std::string>(),
                         long timeoutSeconds = 30);

/// Submit form parameters without putting a bounded telemetry payload in the
/// request line. Web builds retain the GET fallback used by their HTTP shim.
std::string postToHttp(const std::string& url, const std::map<std::string, std::string>& parameters,
                       long timeoutSeconds = 30);

std::string loadFromHttp(const std::string& domain, const std::string& filepath, unsigned short port = PORT_HTTP);

/**
 * Download a URL to a local file. An existing partial file is resumed when the
 * server supports byte ranges; servers that return a full response are handled
 * by safely restarting the file. The callback returns false to cancel.
 */
void downloadHttpFile(const std::string& url, const std::string& filename,
                      const std::function<bool(uint64_t, uint64_t)>& progress = {});


#endif // ENETHTTP_H
