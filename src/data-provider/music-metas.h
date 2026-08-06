#ifndef MUSIC_METAS_H
#define MUSIC_METAS_H

#include <memory>

#include "common/music-meta.h"

struct MusicMetasCore {
    std::vector<MusicMeta> metas;
};

class MusicMetas {
private:
    std::shared_ptr<MusicMetasCore> storage;

public:
    std::string path;

    std::vector<MusicMeta>& metas;

    MusicMetas();
    explicit MusicMetas(std::shared_ptr<MusicMetasCore> storage);

    std::shared_ptr<MusicMetasCore> sharedCore() const;

    void loadFromJson(const json& j);

    void loadFromFile(const std::string& path);

    void loadFromString(const std::string& s);
};

#endif // MUSIC_METAS_H
