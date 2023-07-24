BuildProject({
    projectName = "Better-SimpleBox",
    projectType = "binary",
    debugEvent = function()
        add_defines("_DEBUG")
    end,
    releaseEvent = function()
        add_defines("NDEBUG")
    end,
    exception = true
})
add_defines("_XM_NO_INTRINSICS_=1", "NOMINMAX", "UNICODE", "m128_f32=vector4_f32", "m128_u32=vector4_u32")
add_files("**.cpp")
add_includedirs("../thirdparty/imgui/IMGUI/")
add_includedirs("./")
add_syslinks("User32", "kernel32", "Gdi32", "Shell32", "DXGI", "D3D12", "D3DCompiler")
add_links("Imgui.lib")
add_linkdirs("../thirdparty/imgui/x64/Debug")
after_build(function(target)
    src_path = "shader/"
    os.cp(src_path .. "*", target:targetdir() .. "/shader/")
end)