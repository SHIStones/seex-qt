#pragma once

#include <QString>

namespace seex::altium {

/// Where an Altium integrated library package and its two documents live.
struct LibPkgPaths {
    QString package;
    QString schlib;
    QString pcblib;
    /// True when the package has to be written; an existing one is never
    /// rewritten, because it may list documents this export knows nothing of.
    bool create = false;

    QString directory() const;
};

/// Resolves the package an export targets.
///
/// `existing` wins when it names a file. Otherwise the package is
/// `<directory>/<name>.LibPkg`; if that file already exists it is treated as an
/// existing package, so running the export twice keeps appending to the same
/// libraries instead of replacing them.
LibPkgPaths resolvePackage(const QString &directory, const QString &name, const QString &existing,
                           QString *error = nullptr);

/// Writes the `.LibPkg` for a package that does not exist yet.
bool writePackage(const LibPkgPaths &paths, QString *error = nullptr);

/// The folder a new package goes in when the field is left empty.
QString defaultPackageDirectory();

constexpr const char *kDefaultPackageName = "立创器件库";
constexpr const char *kPackageSchLib = "SCH.SchLib";
constexpr const char *kPackagePcbLib = "PCB.PcbLib";

} // namespace seex::altium
