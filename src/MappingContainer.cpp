#include <iostream>
#include "MappingContainer.h"
#include "vlq.h"

MappingContainer::MappingContainer() {}
MappingContainer::~MappingContainer() {}

int MappingContainer::addName(std::string &name) {
    int index = getNameIndex(name);
    if (index < 0) {
        _names.push_back(name);
        index = (int) _names.size() - 1;
        _names_index[name] = index;
    }
    return index;
}

int MappingContainer::addSource(std::string &source) {
    int index = getSourceIndex(source);
    if (index < 0) {
        _sources.push_back(source);
        index = (int) _sources.size() - 1;
        _sources_index[source] = index;
    }
    return index;
}

int MappingContainer::getGeneratedLines() {
    return _generated_lines;
}

void MappingContainer::sort() {
    auto lineEnd = _mapping_lines.end();
    for (auto lineIterator = _mapping_lines.begin(); lineIterator != lineEnd; ++lineIterator) {
        (*lineIterator)->sort();
    }
}

void MappingContainer::addMapping(Position generated, Position original, int source, int name) {
    createLinesIfUndefined(generated.line);
    _mapping_lines[generated.line]->addMapping(Mapping{generated, original, source, name});
    ++_segment_count;
}

void MappingContainer::createLinesIfUndefined(int generatedLine) {
    if (_generated_lines < generatedLine) {
        _mapping_lines.reserve(generatedLine - _generated_lines + 1);

        // While our last line is not equal (or larger) to our generatedLine we need to add lines
        while (_generated_lines < generatedLine) {
            addLine();
        }
    }
}

void MappingContainer::addVLQMappings(const std::string &mappings_input, std::vector<int> &sources,
                                      std::vector<int> &names, int line_offset, int column_offset) {
    // SourceMap information
    int generatedLine = line_offset;

    // VLQ Decoding
    int value = 0;
    int shift = 0;
    int segment[5] = {column_offset, 0, 0, 0, 0};
    int segmentIndex = 0;

    // `input.len() / 2` is the upper bound on how many mappings the string
    // might contain. There would be some sequence like `A,A,...` or `A;A...`
    // int upperbound = mappings_input.length() / 2;

    auto end = mappings_input.end();
    for (auto it = mappings_input.begin(); it != end; ++it) {
        const char &c = *it;
        if (c == ',' || c == ';') {
            bool hasSource = segmentIndex > 3;
            bool hasName = segmentIndex > 4;

            Position generated = Position{generatedLine, segment[0]};
            Position original = Position{hasSource ? segment[2] : -1, hasSource ? segment[3] : -1};

            addMapping(generated, original, hasSource ? sources[segment[1]] : -1,
                             hasName ? names[segment[4]] : -1);

            if (c == ';') {
                segment[0] = column_offset;
                ++generatedLine;
            }

            segmentIndex = 0;
            continue;
        }

        const int decodedCharacter = decodeBase64Char(c);

        value += (decodedCharacter & VLQ_BASE_MASK) << shift;

        if ((decodedCharacter & VLQ_BASE) != 0) {
            shift += VLQ_BASE_SHIFT;
        } else {
            // The low bit holds the sign.
            if ((value & 1) != 0) {
                value = -value;
            }

            segment[segmentIndex++] += value / 2;
            shift = value = 0;
        }
    }

    // Process last mapping...
    if (segmentIndex > 0) {
        bool hasSource = segmentIndex > 3;
        bool hasName = segmentIndex > 4;

        Position generated = Position{generatedLine, segment[0]};
        Position original = Position{hasSource ? segment[2] : -1, hasSource ? segment[3] : -1};

        addMapping(generated, original, hasSource ? sources[segment[1]] : -1, hasName ? names[segment[4]] : -1);
    }
}

std::string MappingContainer::toVLQMappings() {
    std::stringstream out;

    int previousSource = 0;
    int previousOriginalLine = 0;
    int previousOriginalColumn = 0;
    int previousName = 0;
    bool isFirstLine = true;

    // Sort mappings
    sort();

    auto lineEnd = _mapping_lines.end();
    for (auto lineIterator = _mapping_lines.begin(); lineIterator != lineEnd; ++lineIterator) {
        auto &line = (*lineIterator);
        int previousGeneratedColumn = 0;

        if (!isFirstLine) {
            out << ";";
        }

        bool isFirstSegment = true;
        auto &segments = line->_segments;
        auto segmentsEnd = segments.end();
        for (auto segmentIterator = segments.begin(); segmentIterator != segmentsEnd; ++segmentIterator) {
            Mapping &mapping = *segmentIterator;

            if (!isFirstSegment) {
                out << ",";
            }

            encodeVlq(mapping.generated.column - previousGeneratedColumn, out);
            previousGeneratedColumn = mapping.generated.column;

            int mappingSource = mapping.source;
            if (mappingSource > -1) {
                encodeVlq(mappingSource - previousSource, out);
                previousSource = mappingSource;

                encodeVlq(mapping.original.line - previousOriginalLine, out);
                previousOriginalLine = mapping.original.line;

                encodeVlq(mapping.original.column - previousOriginalColumn, out);
                previousOriginalColumn = mapping.original.column;
            }

            int mappingName = mapping.name;
            if (mappingName > -1) {
                encodeVlq(mappingName - previousName, out);
                previousName = mappingName;
            }

            isFirstSegment = false;
        }

        isFirstLine = false;
    }

    return out.str();
}

Mapping MappingContainer::findClosestMapping(int lineIndex, int columnIndex) {
    if (lineIndex <= _generated_lines) {
        auto &line = _mapping_lines.at(lineIndex);
        auto &segments = line->_segments;
        unsigned int segmentsCount = segments.size();

        int startIndex = 0;
        int stopIndex = segmentsCount - 1;
        int middleIndex = ((stopIndex + startIndex) / 2);
        while (startIndex < stopIndex) {
            Mapping &mapping = segments[middleIndex];
            int diff = mapping.generated.column - columnIndex;
            if (diff > 0) {
                --stopIndex;
            } else if (diff < 0) {
                ++startIndex;
            } else {
                break;
            }

            middleIndex = ((stopIndex + startIndex) / 2);
        }

        return segments[middleIndex];
    }

    return Mapping{Position{-1, -1}, Position{-1, -1}, -1, -1};
}

int MappingContainer::getTotalSegments() {
    return _segment_count;
}

std::vector<std::string> &MappingContainer::getNamesVector() {
    return _names;
}

std::vector<std::string> &MappingContainer::getSourcesVector() {
    return _sources;
}

std::vector<MappingLine *> &MappingContainer::getMappingLinesVector() {
    return _mapping_lines;
}

MappingLine *MappingContainer::addLine(int size) {
    MappingLine *line = new MappingLine(++_generated_lines, size);
    _mapping_lines.push_back(line);
    return line;
}

int MappingContainer::getSourceIndex(std::string &source) {
    auto foundValue = _sources_index.find(source);
    if (foundValue == _sources_index.end()) {
        return -1;
    }
    return foundValue->second;
}

int MappingContainer::getNameIndex(std::string &name) {
    auto foundValue = _names_index.find(name);
    if (foundValue == _names_index.end()) {
        return -1;
    }
    return foundValue->second;
}
