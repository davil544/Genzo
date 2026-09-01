vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO strukturag/libheif
    REF "v1.23.2"
    SHA512 d34ccd40d7f7cc48ff8ceddafd8f7c7004ab0df2a657237fcc715c4bcc453a41946d7db542d32b781875868ee116c451b4b63daaf073530245ec7b117ae60e27
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