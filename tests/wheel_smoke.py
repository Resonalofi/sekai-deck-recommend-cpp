from importlib.resources import files

import sekai_deck_recommend_cpp
from sekai_deck_recommend_cpp import DeckRecommendOptions, SekaiDeckRecommend


def main() -> None:
    package_files = files(sekai_deck_recommend_cpp)
    assert (package_files / "data" / "worldBloomSupportDeckBonusesWL1.json").is_file()
    assert (package_files / "data" / "worldBloomSupportDeckBonusesWL2.json").is_file()
    assert (package_files / "data" / "worldBloomSupportDeckBonusesWL3.json").is_file()
    assert DeckRecommendOptions().to_dict() == {}

    options = DeckRecommendOptions()
    options.world_bloom_event_turn = 3
    options.world_bloom_event_group = 1
    assert options.to_dict() == {
        "world_bloom_event_turn": 3,
        "world_bloom_event_group": 1,
    }
    assert DeckRecommendOptions.from_dict(options.to_dict()).to_dict() == options.to_dict()

    engine = SekaiDeckRecommend()
    try:
        engine.update_musicmetas_from_string(b"[]", "invalid")
    except ValueError as error:
        assert str(error) == "Invalid region: invalid"
    else:
        raise AssertionError("invalid region was accepted")


if __name__ == "__main__":
    main()
