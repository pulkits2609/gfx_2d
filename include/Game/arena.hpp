#pragma once

#include<algorithm>
#include<glm/glm.hpp>
#include"Game/matchRules.hpp"



class Arena{
    private:
        glm::vec2 halfSize{
            MatchRules::ArenaHalfWidth,
            MatchRules::ArenaHalfHeight
        };

    public:
        glm::vec2 GetHalfSize() const{
            return halfSize;
        }

        //keeps the player’s entire circular hitbox inside the arena.
        glm::vec2 ClampPosition(
            const glm::vec2& position,
            float radius
        ) const{
            return {
                std::clamp(
                    position.x,
                    -halfSize.x + radius,
                    halfSize.x - radius
                ),
                std::clamp(
                    position.y,
                    -halfSize.y + radius,
                    halfSize.y - radius
                )
            };
        }

        //origin must be inside the arena; direction must be normalized
        //finds how far a shot can travel before reaching a wall. It calculates the distance to the relevant X and Y boundaries, then selects the nearest one.
        float GetRayExitDistance(
            const glm::vec2& origin,
            const glm::vec2& direction,
            float maximumDistance
        ) const{
            float distance = maximumDistance;

            if(direction.x > 0.0f){
                distance = std::min(
                    distance,
                    (halfSize.x - origin.x) / direction.x
                );
            }
            else if(direction.x < 0.0f){
                distance = std::min(
                    distance,
                    (-halfSize.x - origin.x) / direction.x
                );
            }

            if(direction.y > 0.0f){
                distance = std::min(
                    distance,
                    (halfSize.y - origin.y) / direction.y
                );
            }
            else if(direction.y < 0.0f){
                distance = std::min(
                    distance,
                    (-halfSize.y - origin.y) / direction.y
                );
            }

            return std::max(0.0f, distance);
        }
};