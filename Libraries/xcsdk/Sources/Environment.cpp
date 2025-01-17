/**
 Copyright (c) 2015-present, Facebook, Inc.
 All rights reserved.

 This source code is licensed under the BSD-style license found in the
 LICENSE file in the root directory of this source tree.
 */

#include <xcsdk/Environment.h>
#include <libutil/Base.h>
#include <libutil/Filesystem.h>
#include <libutil/FSUtil.h>
#include <process/Context.h>
#include <process/User.h>

using xcsdk::Environment;
using libutil::Filesystem;
using libutil::FSUtil;

static std::string
UserDeveloperRootLink(std::string const &userHomeDirectory)
{
    return userHomeDirectory + "/.xcsdk/xcode_select_link";
}

static std::string
VarDbDirectory()
{
    return "/var/db";
}

static std::string
PrimaryDeveloperRootLink()
{
    return "/var/db/xcode_select_link";
}

static std::string
SecondaryDeveloperRootLink()
{
    return "/usr/share/xcode-select/xcode_dir_path";
}

static std::string
ResolveDeveloperRoot(Filesystem const *filesystem, std::string const &path)
{
    /*
     * Support finding the developer directory inside an application directory.
     */
    std::string expandedPath = path;
    std::string application = expandedPath + "/Contents/Developer";
    if (filesystem->type(application) == Filesystem::Type::Directory) {
        expandedPath = application;
    }

    if (auto resolvedPath = filesystem->readSymbolicLinkCanonical(expandedPath)) {
        expandedPath = *resolvedPath;
    }

    return expandedPath;
}

ext::optional<std::string> Environment::
DeveloperRoot(process::User const *user, process::Context const *processContext, Filesystem const *filesystem)
{
    if (auto path = processContext->environmentVariable("DEVELOPER_DIR")) {
        if ((path = processContext->shellExpand(*path))) {
            return ResolveDeveloperRoot(filesystem, *path);
        }
    }

    ext::optional<std::string> userHomeDirectory = user->userHomeDirectory();

    if (userHomeDirectory) {
        if (auto path = filesystem->readSymbolicLinkCanonical(UserDeveloperRootLink(*userHomeDirectory))) {
            return path;
        }
    }

    if (auto path = filesystem->readSymbolicLinkCanonical(PrimaryDeveloperRootLink())) {
        return path;
    }

    if (auto path = filesystem->readSymbolicLinkCanonical(SecondaryDeveloperRootLink())) {
        return path;
    }

    /*
     * Fall back to a set of known directories.
     */
    std::vector<std::string> defaults = {
        "/Applications/Xcode.app/Contents/Developer",
        "/Developer",
    };
    for (std::string const &path : defaults) {
        if (filesystem->type(path) == Filesystem::Type::Directory) {
            return path;
        }
    }

    return ext::nullopt;
}

bool Environment::
WriteDeveloperRoot(libutil::Filesystem *filesystem, ext::optional<std::string> const &path)
{
    /*
     * Proceed only if the path is an invalid developer directory.
     */
    if (!path) {
        return false;
    }
    std::string normalized = FSUtil::NormalizePath(*path);
    std::string resolved = ResolveDeveloperRoot(filesystem, normalized);
    if (!filesystem->exists(resolved)) {
        fprintf(stderr, "error: invalid developer directory: '%s'\n", normalized.c_str());
        return false;
    }

    /*
     * Remove any existing link.
     */
    if (filesystem->exists(PrimaryDeveloperRootLink())) {
        if (!filesystem->removeFile(PrimaryDeveloperRootLink())) {
            return false;
        }
    }

    /*
     * Create '/var/db' directory if not exists.
     */
    if (!filesystem->exists(VarDbDirectory())) {
        if (!filesystem->createDirectory(VarDbDirectory(), true)) {
            return false;
        }
    }

    /*
     * Write the new link.
     */
    if (!filesystem->writeSymbolicLink(resolved, PrimaryDeveloperRootLink(), true)) {
        return false;
    }

    return true;
}
