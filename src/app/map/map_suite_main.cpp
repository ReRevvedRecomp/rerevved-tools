#include "map/map_suite.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

int main(int argc, char** argv)
{
    using namespace rerevved::tools;
    std::filesystem::path mapsFolder;
    if (argc >= 3 && std::string_view(argv[1]) == "--save-maps")
    {
        mapsFolder = std::filesystem::path(argv[2]);
        argv += 2;
        argc -= 2;
    }
    if (argc < 3)
    {
        std::cerr << "Usage: rerevved-map-suite [--save-maps <folder>] <plan file> <output folder> [<maps folder>...]\n"
                     "Writes one map pack per [pack] of the plan into the output folder. Pack IDs, scenario IDs\n"
                     "and MAP names already used in the output folder or any maps folder count as clashes.\n"
                     "--save-maps also saves each map as <pack id>--<MAP>.rrmap for the editor.\n";
        return 2;
    }
    const std::filesystem::path planFile(argv[1]);
    std::ifstream               input(planFile, std::ios::binary);
    if (!input)
    {
        std::cerr << "The plan file " << argv[1] << " could not be opened.\n";
        return 1;
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto              plan = ParseMapSuitePlan(text);
    if (!plan)
    {
        std::cerr << plan.error() << '\n';
        return 1;
    }
    if (const auto read = ResolveMapSuiteSources(*plan, std::filesystem::absolute(planFile).parent_path()); !read)
    {
        std::cerr << read.error() << '\n';
        return 1;
    }
    MapPackUsage usage;
    for (int index = 3; index < argc; ++index)
    {
        std::error_code error;
        if (!std::filesystem::is_directory(std::filesystem::path(argv[index]), error))
        {
            std::cerr << "The maps folder " << argv[index] << " does not exist.\n";
            return 1;
        }
        AddMapPackUsage(usage, std::filesystem::path(argv[index]));
    }
    const auto receipts = ExportMapSuite(*plan, std::filesystem::path(argv[2]), usage, mapsFolder);
    if (!receipts)
    {
        std::cerr << receipts.error() << '\n';
        return 1;
    }
    for (const auto& receipt : *receipts)
    {
        std::cout << FormatMapPackReceipt(receipt) << '\n';
    }
    return 0;
}
