vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO strukturag/libheif
    REF "v1.23.1"
    SHA512 ebb0bf74d6d1ae2d39a7cefcec0f4a04006fee330e147c49c863f6d4febb45e82ccebf36cc4e30812bd3591918ee8964fbde9a3a4793c5f60eaf30a0e011add0
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