#pragma once

class FaceSampleImporter
{
public:
    struct Result
    {
        int importedCount = 0;
        int skippedCount = 0;
        int failedCount = 0;
    };

    static Result importSamples();
};
