# 外部依存の取得。版はここで固定する。
#
# raylib 5.5（基準版。最新版という意味ではない）
#   tag    : 5.5
#   commit : c1ab645ca298a2801097931d1079b10ff7eb9df8（2026-10-28に解決）
#   archive: https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
#   sha256 : aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e
#
# オフライン時は -DFETCHCONTENT_SOURCE_DIR_RAYLIB=/path/to/raylib-5.5 を指定する。
# 取得できない場合は構成エラーとし、黙って省略しない。

include(FetchContent)

set(WATERLAB_RAYLIB_VERSION "5.5")

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(OPENGL_VERSION "3.3" CACHE STRING "" FORCE)

FetchContent_Declare(raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
    URL_HASH SHA256=aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(raylib)

if(NOT TARGET raylib)
    message(FATAL_ERROR "raylib ${WATERLAB_RAYLIB_VERSION} を取得できませんでした。"
                        "FETCHCONTENT_SOURCE_DIR_RAYLIB でソースの所在を指定してください。")
endif()
