#include "golang_analyzer.h"


void make_comment_map(std::map<uint64_t, std::string>& comment_map, uint64_t* func_size, const GOPCLNTAB* gopclntab, duint func_info_addr)
{
    comment_map.clear();

    std::vector<std::map<uint64_t, std::string>> comment_info;
    comment_info.push_back(init_file_line_map(gopclntab, func_info_addr, func_size));
    comment_info.push_back(init_sp_map(gopclntab, func_info_addr));

    for (auto& i : comment_info)
    {
        for (auto& j : i)
        {
            if (comment_map.count(j.first))
            {
                comment_map[j.first] += " " + j.second;
            }
            else
            {
                comment_map[j.first] = j.second;
            }
        }
    }
}


bool analyze_functions(const GOPCLNTAB* gopclntab)
{
    if (gopclntab == NULL)
    {
        return false;
    }

    for (uint32_t i = 0; i < gopclntab->func_num; i++)
    {
        uint64_t functab_field_size = gopclntab->version == GO_VERSION::GO_118 || gopclntab->version == GO_VERSION::GO_120 ? 4 : gopclntab->pointer_size;

        uint64_t func_addr_value = 0;
        if (!read_dbg_memory(gopclntab->func_list_base + (duint)i * functab_field_size * 2, &func_addr_value, functab_field_size))
        {
            return false;
        }
        if (gopclntab->version == GO_VERSION::GO_118 || gopclntab->version == GO_VERSION::GO_120)
        {
            uint64_t text_addr = 0;
            if (!read_dbg_memory(gopclntab->addr + 8 + (uint32_t)gopclntab->pointer_size * 2, &text_addr, gopclntab->pointer_size))
            {
                return false;
            }
            func_addr_value += text_addr;
        }
        uint64_t func_info_offset = 0;
        if (!read_dbg_memory(gopclntab->func_list_base + (duint)i * functab_field_size * 2 + functab_field_size, &func_info_offset, functab_field_size))
        {
            return false;
        }
        duint func_info_addr = gopclntab->addr + (duint)func_info_offset;
        if (gopclntab->version == GO_VERSION::GO_116 || gopclntab->version == GO_VERSION::GO_118 || gopclntab->version == GO_VERSION::GO_120)
        {
            func_info_addr = gopclntab->func_list_base + (duint)func_info_offset;
        }
        uint64_t func_entry_value = 0;
        if (!read_dbg_memory(func_info_addr, &func_entry_value, functab_field_size))
        {
            return false;
        }
        uint64_t func_name_offset = 0;
        if (!read_dbg_memory(func_info_addr + functab_field_size, &func_name_offset, 4))
        {
            return false;
        }

        char func_name[MAX_PATH] = { 0 };
        duint func_name_base = gopclntab->addr;
        if (gopclntab->version == GO_VERSION::GO_116 || gopclntab->version == GO_VERSION::GO_118 || gopclntab->version == GO_VERSION::GO_120)
        {
            uint64_t tmp_value = 0;
            if (!read_dbg_memory(gopclntab->addr + 8 + (uint32_t)gopclntab->pointer_size * (gopclntab->version == GO_VERSION::GO_116 ? 2 : 3), &tmp_value, gopclntab->pointer_size))
            {
                return false;
            }
            func_name_base = gopclntab->addr + (duint)tmp_value;
        }
        if (!read_dbg_memory(func_name_base + (duint)func_name_offset, func_name, sizeof(func_name)))
        {
            return false;
        }
        func_name[sizeof(func_name) - 1] = '\0';
        DbgSetLabelAt((duint)func_addr_value, func_name);

        uint32_t args_num = 0;
        if (!read_dbg_memory(func_info_addr + functab_field_size + 4, &args_num, 4))
        {
            return false;
        }
        if (args_num >= 0x80000000)
        {
            args_num = 0;
        }

        uint64_t func_size = 0;
        std::map<uint64_t, std::string> comment_map;
        make_comment_map(comment_map, &func_size, gopclntab, func_info_addr);

        DbgFunctionAdd((duint)func_addr_value, (duint)func_addr_value + (duint)func_size - 1);

        if (get_line_enabled() && comment_map.size() > 0)
        {
            for (auto& j : comment_map)
            {
                DbgSetCommentAt((duint)func_addr_value + (duint)j.first, j.second.c_str());
            }
            char func_comment[MAX_COMMENT_SIZE] = { 0 };
            char comment[MAX_COMMENT_SIZE] = { 0 };
            DbgGetCommentAt((duint)func_addr_value, comment);
            _snprintf_s(func_comment, sizeof(func_comment), _TRUNCATE, "%s args:%d %s", func_name, args_num, comment_map.at(0).c_str());
            DbgSetCommentAt((duint)func_addr_value, func_comment);
        }
    }

    return true;
}


bool command_callback(int argc, char* argv[])
{
    if (argc < 1)
    {
        return false;
    }

    if (strstr(argv[0], "help"))
    {
        goanalyzer_logputs("Golang Analyzer: Help\n"
            "Command:\n"
            "    GoAnalyzer.help\n"
            "    GoAnalyzer.analyze\n"
            "    GoAnalyzer.line.enable\n"
            "    GoAnalyzer.line.disable");
    }
    else if (strstr(argv[0], "analyze"))
    {
        GOPCLNTAB gopclntab_base = {};
        if (!get_gopclntab(&gopclntab_base))
        {
            goanalyzer_logputs("Golang Analyzer: Failed to get gopclntab");
            return false;
        }
        if (!analyze_file_name(&gopclntab_base))
        {
            goanalyzer_logputs("Golang Analyzer: Failed to get file name");
            return false;
        }
        if (!analyze_functions(&gopclntab_base))
        {
            goanalyzer_logputs("Golang Analyzer: Failed to analyze functions");
            return false;
        }
        goanalyzer_logputs("Golang Analyzer: Analyze");
    }
    else if (strstr(argv[0], "line.enable"))
    {
        set_line_enabled(true);
    }
    else if (strstr(argv[0], "line.disable"))
    {
        set_line_enabled(false);
    }

    return true;
}


bool init_analyzer_plugin()
{
    _plugin_registercommand(pluginHandle, "GoAnalyzer.help", command_callback, false);
    _plugin_registercommand(pluginHandle, "GoAnalyzer.analyze", command_callback, false);
    _plugin_registercommand(pluginHandle, "GoAnalyzer.line.enable", command_callback, false);
    _plugin_registercommand(pluginHandle, "GoAnalyzer.line.disable", command_callback, false);

    return true;
}


bool stop_analyzer_plugin()
{
    _plugin_unregistercommand(pluginHandle, "GoAnalyzer.help");
    _plugin_unregistercommand(pluginHandle, "GoAnalyzer.analyze");
    _plugin_unregistercommand(pluginHandle, "GoAnalyzer.line.enable");
    _plugin_unregistercommand(pluginHandle, "GoAnalyzer.line.disable");

    return true;
}


void setup_analyzer_plugin()
{
}
