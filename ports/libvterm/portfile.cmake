vcpkg_download_distfile(ARCHIVE
    URLS "https://www.leonerd.org.uk/code/libvterm/libvterm-${VERSION}.tar.gz"
    FILENAME "libvterm-${VERSION}.tar.gz"
    SHA512 0117f71988d2c3c1b5e46d0778ce491a8ab9033f3afc9a9905b6d52d0e2adb59ee101667826281470e4c9de5178cbe966f4ecb2c7673e9644c7ca3508491bb49
)

vcpkg_extract_source_archive(SOURCE_PATH
    ARCHIVE "${ARCHIVE}"
)

# The upstream build uses a Makefile, so the port brings its own CMake file.
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/unofficial-libvterm-config.cmake.in" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(
    PACKAGE_NAME unofficial-libvterm
    CONFIG_PATH share/unofficial-libvterm
)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
