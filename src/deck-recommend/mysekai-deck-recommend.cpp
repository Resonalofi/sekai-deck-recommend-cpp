#include "deck-recommend/mysekai-deck-recommend.h"

RecommendResult MysekaiDeckRecommend::recommendMysekaiDeck(
    int eventId, 
    const DeckRecommendConfig &config, 
    int specialCharacterId
)
{
    auto eventConfig = eventService.getEventConfig(eventId, specialCharacterId);
    if (!eventConfig.eventType) {
        throw std::runtime_error("Event type not found for " + std::to_string(eventId));
    }

    auto cfg = config;
    cfg.target = RecommendTarget::Mysekai;
    cfg.keepAfterTrainingState = true;

    auto userCards = dataProvider.userData->userCards;
    // 全当期：把本活动的当期卡视为已拥有
    if (cfg.ownAllBonusCards)
        baseRecommend.addEventBonusCardsToPool(eventId, userCards, cfg);
    return baseRecommend.recommendHighScoreDeck(userCards,
        this->mysekaiEventCalculator.getMysekaiEventPointFunction(), 
        cfg, 
        Enums::LiveType::multi_live,
        eventConfig
    );
}