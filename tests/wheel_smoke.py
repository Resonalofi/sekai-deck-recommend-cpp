from importlib.resources import files

import sekai_deck_recommend_cpp
from sekai_deck_recommend_cpp import (
    DeckRecommendOptions,
    DeckRecommendResult,
    RecommendCard,
    RecommendDeck,
    RecommendSupportDeck,
    SekaiDeckRecommend,
)


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

    options.algorithms = ["dfs", "ga"]
    options.parallel_algorithms = True
    assert DeckRecommendOptions.from_dict(options.to_dict()).to_dict() == options.to_dict()

    # Partial override dictionaries preserve omitted fields and round-trip as-is.
    options.area_item_config = {"items": {"12": 15}}
    options.character_rank_config = {"overrides": {"17": 60}}
    options.honor_config = {"overrides": {"12345": {"enabled": False}}}
    options.mysekai_config = {"canvas": {"include": [123]}}
    assert DeckRecommendOptions.from_dict(options.to_dict()).to_dict() == options.to_dict()

    # 引擎侧计时与来源算法随结果返回
    result = DeckRecommendResult()
    assert result.total_ms == 0.0
    assert result.algorithm_ms == {}
    assert result.to_dict() == {"decks": [], "total_ms": 0.0, "algorithm_ms": {}}
    assert RecommendDeck().algorithms == []
    assert DeckRecommendResult.from_dict(result.to_dict()).to_dict() == result.to_dict()

    support_card = RecommendCard()
    support_card.card_id = 123
    support_card.level = 60
    support_card.master_rank = 5
    support_card.skill_level = 4
    support_card.episode1_read = True
    support_card.support_deck_bonus_rate = 18.5
    support_deck = RecommendSupportDeck()
    support_deck.character_id = 17
    support_deck.capacity = 20
    support_deck.bonus_rate = 18.5
    support_deck.cards = [support_card]
    deck = RecommendDeck()
    deck.wl_sub_deck = support_deck
    assert RecommendDeck.from_dict(deck.to_dict()).to_dict() == deck.to_dict()

    engine = SekaiDeckRecommend()
    try:
        engine.update_musicmetas_from_string(b"[]", "invalid")
    except ValueError as error:
        assert str(error) == "Invalid region: invalid"
    else:
        raise AssertionError("invalid region was accepted")


if __name__ == "__main__":
    main()
