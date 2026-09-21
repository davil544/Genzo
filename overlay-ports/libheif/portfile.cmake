vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO strukturag/libheif
    REF "v1.23.4"
    SHA512 f33b216fd550ad1f7d65c76977bea77e4447875e1c84a868d620406bc2530ce8425e781e8052f4cddab49b87e2a6184a58edcd3782bb12ec6414a99bf8663e58
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DWITH_KVAZAAR=ON
        -DWITH_X265=OFF
        -DWITH_EXAMPLES=OFF
        -DWITH_LIBDE265=ON
        -DENABLE_PLUGIN_LOADING=OFF
        -DBUILD_TESTING=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/libheif)
vcpkg_fixup_pkgconfig()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL "${SOURCE_PATH}/COPYING"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME copyright)