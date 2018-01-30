//
// Created by sebastian on 15.11.17.
//

#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <lamure/vt/pre/Preprocessor.h>
#include <lamure/vt/pre/AtlasFile.h>

using namespace vt::pre;

Bitmap::PIXEL_FORMAT parsePixelFormat(const char *formatStr){
    if(std::strcmp(formatStr, "r") == 0){
        return Bitmap::PIXEL_FORMAT::R8;
    }else if(std::strcmp(formatStr, "rgb") == 0){
        return Bitmap::PIXEL_FORMAT::RGB8;
    }else if(std::strcmp(formatStr, "rgba") == 0){
        return Bitmap::PIXEL_FORMAT::RGBA8;
    }else{
        throw std::runtime_error("Invalid pixel format given.");
    }
}

AtlasFile::LAYOUT parseFileFormat(const char *formatStr){
    if(std::strcmp(formatStr, "raw") == 0){
        return AtlasFile::LAYOUT::RAW;
    }else if(std::strcmp(formatStr, "packed") == 0){
        return AtlasFile::LAYOUT::PACKED;
    }else{
        throw std::runtime_error("Invalid file format given.");
    }
}

const char *printLayout(AtlasFile::LAYOUT layout){
    switch(layout){
        case AtlasFile::LAYOUT::RAW:
            return "raw";
        case AtlasFile::LAYOUT::PACKED:
            return "packed";
    }
}

const char *printPxFormat(Bitmap::PIXEL_FORMAT pxFormat){
    switch(pxFormat){
        case Bitmap::PIXEL_FORMAT::R8:
            return "R8";
        case Bitmap::PIXEL_FORMAT::RGB8:
            return "RGB8";
        case Bitmap::PIXEL_FORMAT::RGBA8:
            return "RGBA8";
    }
}

int process(const int argc, const char **argv){
    if(argc != 11){
        std::cout << "Wrong count of parameters." << std::endl;
        std::cout << "Expected parameters:" << std::endl;
        std::cout << "\t<image file> <image pixel format (r, rgb, rgba)>" << std::endl;
        std::cout << "\t<image width> <image height>" << std::endl;
        std::cout << "\t<tile width> <tile height> <padding>" << std::endl;
        std::cout << "\t<out file (without extension)> <out file format (raw, packed)> <out pixel format (r, rgb, rgba)>" << std::endl;
        std::cout << "\t<max memory usage>" << std::endl;

        return 1;
    }

    Bitmap::PIXEL_FORMAT inPixelFormat;
    Bitmap::PIXEL_FORMAT outPixelFormat;
    AtlasFile::LAYOUT outFileFormat;

    try {
        inPixelFormat = parsePixelFormat(argv[1]);
    }catch(std::runtime_error error){
        std::cout << "Invalid input pixel format given: \"" << argv[1] << "\"." << std::endl;

        return 1;
    }

    try {
        outPixelFormat = parsePixelFormat(argv[9]);
    }catch(std::runtime_error &error){
        std::cout << "Invalid output pixel format given: \"" << argv[9] << "\"." << std::endl;

        return 1;
    }

    try {
        outFileFormat = parseFileFormat(argv[8]);
    }catch(std::runtime_error &error){
        std::cout << "Invalid output file format given: \"" << argv[8] << "\"." << std::endl;

        return 1;
    }

    std::stringstream stream;

    uint64_t imageWidth;
    uint64_t imageHeight;
    size_t tileWidth;
    size_t tileHeight;
    size_t padding;
    size_t maxMemory;

    stream.write(argv[2], std::strlen(argv[2]));

    if (!(stream >> imageWidth)) {
        std::cout << imageWidth << std::endl;
        std::cerr << "Invalid image width \"" << argv[2] << "\"." << std::endl;

        return 1;
    }

    stream.clear();
    stream.write(argv[3], std::strlen(argv[3]));

    if (!(stream >> imageHeight)) {
        std::cerr << "Invalid image height \"" << argv[3] << "\"." << std::endl;

        return 1;
    }

    stream.clear();
    stream.write(argv[4], std::strlen(argv[4]));

    if (!(stream >> tileWidth)) {
        std::cerr << "Invalid tile width \"" << argv[4] << "\"." << std::endl;

        return 1;
    }

    stream.clear();
    stream.write(argv[5], std::strlen(argv[5]));

    if (!(stream >> tileHeight)) {
        std::cerr << "Invalid tile height \"" << argv[5] << "\"." << std::endl;

        return 1;
    }

    stream.clear();
    stream.write(argv[6], std::strlen(argv[6]));

    if (!(stream >> padding)) {
        std::cerr << "Invalid padding \"" << argv[6] << "\"." << std::endl;

        return 1;
    }

    stream.clear();
    stream.write(argv[10], std::strlen(argv[10]));

    if (!(stream >> maxMemory)) {
        std::cerr << "Invalid maximum memory size \"" << argv[10] << "\"." << std::endl;

        return 1;
    }

    Preprocessor pre(argv[0], inPixelFormat, imageWidth, imageHeight);

    pre.setOutput(argv[7], outPixelFormat, outFileFormat, tileWidth, tileHeight, padding);
    pre.run(maxMemory);

    return 0;
}

int info(const int argc, const char **argv){
    if(argc != 1){
        std::cout << "Wrong count of parameters." << std::endl;
        std::cout << "Expected parameters:" << std::endl;
        std::cout << "\t<processed image>" << std::endl;

        return 1;
    }

    AtlasFile *atlas = nullptr;

    try {
        atlas = new AtlasFile(argv[0]);
    }catch(std::runtime_error &error){
        std::cout << "Could not open file \"" << argv[0] << "\"." << std::endl;

        return 1;
    }

    std::cout << "Information for file \"" << argv[0] << "\":" << std::endl;
    std::cout << "\torig. dim.: " << atlas->getImageWidth() << " px x " << atlas->getImageHeight() << " px" << std::endl;
    std::cout << "\ttile. dim.: " << atlas->getTileWidth() << " px x " << atlas->getTileHeight() << " px" << std::endl;
    std::cout << "\tpadding   : " << atlas->getPadding() << " px" << std::endl;
    std::cout << "\tlayout    : " << printLayout(atlas->getFormat()) << std::endl;
    std::cout << "\tpx format : " << printPxFormat(atlas->getPixelFormat()) << std::endl;
    std::cout << "\tlevels    : " << atlas->getDepth() << std::endl;
    std::cout << "\ttiles     : " << atlas->getFilledTiles() << " / " << atlas->getTotalTiles() << std::endl;
    std::cout << std::endl;

    delete atlas;

    return 0;
}

int extract(const int argc, const char **argv){
    if(argc != 3){
        std::cout << "Wrong count of parameters." << std::endl;
        std::cout << "Expected parameters:" << std::endl;
        std::cout << "\t<processed image>" << std::endl;
        std::cout << "\t<level>" << std::endl;
        std::cout << "\t<out file>" << std::endl;

        return 1;
    }

    AtlasFile *atlas;

    try {
        atlas = new AtlasFile(argv[0]);
    }catch(std::runtime_error &error){
        std::cout << "Could not open file \"" << argv[0] << "\"." << std::endl;

        return 1;
    }

    std::stringstream stream;

    uint32_t level;

    stream.write(argv[1], std::strlen(argv[1]));

    if (!(stream >> level) || level >= atlas->getDepth()) {
        std::cerr << "Invalid level \"" << argv[1] << "\"." << std::endl;

        return 1;
    }

    atlas->extractLevel(level, argv[2]);

    std::cout << "Extracted level " << level << " to \"" << argv[2] << "\"." << std::endl;
    std::cout << "Dimensions  : " << (atlas->getInnerTileWidth() * QuadTree::getWidthOfLevel(level)) << " px x " << (atlas->getInnerTileHeight() * QuadTree::getWidthOfLevel(level)) << " px" << std::endl;
    std::cout << "Pixel Format: " << printPxFormat(atlas->getPixelFormat()) << std::endl;

    delete atlas;

    return 0;
}

int main(const int argc, const char **argv){
    if(argc >= 2){
        if(std::strcmp(argv[1], "process") == 0){
            return process(argc - 2, (const char**)((size_t)argv + 2 * sizeof(char*)));
        }else if(std::strcmp(argv[1], "info") == 0){
            return info(argc - 2, (const char**)((size_t)argv + 2 * sizeof(char*)));
        }else if(std::strcmp(argv[1], "extract") == 0){
            return extract(argc - 2, (const char**)((size_t)argv + 2 * sizeof(char*)));
        }
    }

    std::cout << "Expected instruction." << std::endl;
    std::cout << "Available:" << std::endl;
    std::cout << "\tprocess - to preprocess an image" << std::endl;
    std::cout << "\tinfo - to read meta information of preprocessed image" << std::endl;
    std::cout << "\textract - to extract a certain level of detail from preprocessed image" << std::endl;
    std::cout << std::endl;

    return 1;
};