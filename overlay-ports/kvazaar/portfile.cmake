vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ultravideo/kvazaar
    REF "v2.3.0"
    SHA512 b416e24a887a6e83b25530c6d47675fb176568c04295c944094041629b585b968133465605b9ee85e0ebebe1041774d5670cb90ee76271cb2f5c8059244297b5
    HEAD_REF master
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_TESTS=OFF
)

vcpkg_cmake_build()
vcpkg_cmake_install()
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL "${SOURCE_PATH}/LICENSE"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME copyright)