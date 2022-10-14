// Pedro Vernetti Gonçalves

#include <iostream>
#include <chrono>

using byte = uint8_t;

#include "YUVVideo.hpp"

int main( int argc, char * argv[] )
{
    std::string input, output;
    uint32_t w, h;
    if (argc < 4)
    {
        std::cerr << "Usage: " << argv[0] << " INPUT_YUV_FILE WIDTH HEIGHT\n\n";
        return 1;
    }
    input = argv[1];
    output = input.substr(0, input.find_last_of('.')) + ".compressed_yuv";
    w = std::stoi(argv[2]);
    h = std::stoi(argv[3]);

    YUVVideo v(input, w, h);
    if (!v)
    {
        std::cerr << "Couldn't open '" << input << "'";
        return 2;
    }

    std::vector<std::pair<size_t, size_t>> temporallyRedundantBlocks;
    std::chrono::high_resolution_clock::time_point begin, end;
    for (size_t i = 1; i < v.totalFrames(); i++)
    {
        begin = std::chrono::high_resolution_clock::now();
        temporallyRedundantBlocks = v.temporalRedundancies(0, i, 8);
        end = std::chrono::high_resolution_clock::now();
        std::cout << "Frame #" << i << " analyzed against frame #0 in "
                  << std::chrono::duration<double, std::milli>(end - begin).count()
                  << " ms\n";
    }
    std::cout << "\n";

    return 0;
}
