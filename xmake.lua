add_rules("mode.debug", "mode.release")

option("jsonrpcpp_build_tests", function ()
    set_default(false)
    set_showmenu(true)
    set_description("Build tests")
end)

option("jsonrpcpp_external_nlohmann_json", function ()
    set_default(nil)
    set_showmenu(true)
    set_description("External nlohmann_json package directory (if not set, use xmake repo)")
end)

if not has_config("jsonrpcpp_external_nlohmann_json") then
    add_requires("nlohmann_json 3.12.0")
end

target("jsonrpcpp", function ()
    set_kind("headeronly")
    set_languages("c++17")
    add_headerfiles("include/(**.hpp)")
    add_includedirs("include", {public = true})
    if has_config("jsonrpcpp_external_nlohmann_json") then
        add_includedirs(get_config("jsonrpcpp_external_nlohmann_json"), {public = true})
    else
        add_packages("nlohmann_json", {public = true})
    end
end)

if has_config("jsonrpcpp_build_tests") then
    target("tests", function ()
        set_kind("binary")
        set_languages("c++17")
        add_files("tests/*.cpp")
        add_includedirs("tests")
        add_deps("jsonrpcpp")
        if has_config("jsonrpcpp_external_nlohmann_json") then
            add_includedirs(get_config("jsonrpcpp_external_nlohmann_json"))
        else
            add_packages("nlohmann_json")
        end
        if is_plat("linux", "macosx") then
            add_syslinks("pthread")
        end
    end)
end
