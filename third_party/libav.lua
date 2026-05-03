group("third_party")
  include("libav/libavcodec/premake5.lua")
  include("libav/libavutil/premake5.lua")

  project("libavutil")
    filter("platforms:Windows")
      buildoptions({
        "/wd4113", -- function parameter lists differ
      })
    filter({})
