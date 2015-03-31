/*
 * Copyright (C) 2008-2015 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "naxxramas.h"


enum Spells
{
    SPELL_MORTAL_WOUND      = 25646,
    SPELL_ENRAGE            = 28371,
    SPELL_DECIMATE          = 28374,
    SPELL_BERSERK           = 26662,
    SPELL_INFECTED_WOUND    = 29306,
    SPELL_INFECTED_AURA     = 29307,
};

enum Creatures
{
    NPC_ZOMBIE              = 16360
};

Position const PosSummon[3] =
{
    {3267.9f, -3172.1f, 297.42f, 0.94f},
    {3253.2f, -3132.3f, 297.42f, 0},
    {3308.3f, -3185.8f, 297.42f, 1.58f},
};

enum Events
{
    EVENT_WOUND     = 1,
    EVENT_ENRAGE,
    EVENT_DECIMATE,
    EVENT_BERSERK,
    EVENT_SUMMON,
    EVENT_EATEN,
};

#define EMOTE_NEARBY    "Gluth spots a nearby zombie to devour!"
#define EMOTE_DECIMATE  "Gluth decimates all nearby flesh!"
#define EMOTE_ENRAGE    "Gluth becomes enraged!"

class boss_gluth : public CreatureScript
{
public:
    boss_gluth() : CreatureScript("boss_gluth") { }

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new boss_gluthAI(creature);
    }

    struct boss_gluthAI : public BossAI
    {
        boss_gluthAI(Creature* creature) : BossAI(creature, BOSS_GLUTH)
        {
            // Do not let Gluth be affected by zombies' debuff
            me->ApplySpellImmune(0, IMMUNITY_ID, SPELL_INFECTED_WOUND, true);
            
        }

        void MoveInLineOfSight(Unit* who) override
        {
            if (who->GetEntry() == NPC_ZOMBIE && me->IsWithinDistInMap(who, 10))
            {
                SetGazeOn(who);
                /// @todo use a script text
                me->TextEmote(EMOTE_NEARBY, nullptr, true);
                AttackGluth(who->ToCreature());
                me->SetFacingToObject(who);
                me->GetMotionMaster()->MoveChase(who);
            }
            else
                BossAI::MoveInLineOfSight(who);
        }

        void AttackGluth(Creature* who)
        {
            who->SetReactState(REACT_PASSIVE);
            who->AddThreat(me, 9999999);
            who->AI()->AttackStart(me);
            who->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_TAUNT, true);
            who->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_ATTACK_ME, true);
        }

        void AttackGluthDec(ObjectGuid id)
        {
            Creature* summon = me->GetMap()->GetCreature(id);
            if (summon && summon->GetEntry() == NPC_ZOMBIE && summon->IsAlive())
            {
                summon->SetWalk(true);
                summon->SetHealth(summon->GetMaxHealth() * 0.05);
                AttackGluth(summon);
            }
        }

        void EatZombie(Unit* who)
        {
            if (who->GetEntry() == NPC_ZOMBIE)
            {
                if (me->IsWithinDist(who, 10.0f, true))
                {
                    me->SetFacingToObject(who);
                    me->GetMotionMaster()->MoveChase(who);
                    me->Kill(who);
                    me->ModifyHealth(int32(me->CountPctFromMaxHealth(5)));
                    me->Attack(me->GetVictim(), true);
                    me->GetMotionMaster()->MoveChase(me->GetVictim());
                }
            }
        }

        void EnterCombat(Unit* /*who*/) override
        {
            _EnterCombat();
            events.ScheduleEvent(EVENT_WOUND, 10000);
            events.ScheduleEvent(EVENT_ENRAGE, 15000);
            events.ScheduleEvent(EVENT_DECIMATE, 104000);
            events.ScheduleEvent(EVENT_BERSERK, 7*60000);
            events.ScheduleEvent(EVENT_SUMMON, 15000);
            events.ScheduleEvent(EVENT_EATEN, 1000);
        }

        void JustSummoned(Creature* summon) override
        {
            if (summon->GetEntry() == NPC_ZOMBIE)
            {
                if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM, 0, 100, true))
                    summon->AI()->AttackStart(target);
                summon->AddAura(SPELL_INFECTED_AURA, summon);
            }
            summons.Summon(summon);
        }

        void UpdateAI(uint32 diff) override
        {
            if (!UpdateVictimWithGaze() || !CheckInRoom())
                return;

            events.Update(diff);

            while (uint32 eventId = events.ExecuteEvent())
            {
                switch (eventId)
                {
                    case EVENT_WOUND:
                        DoCastVictim(SPELL_MORTAL_WOUND);
                        events.ScheduleEvent(EVENT_WOUND, 10000);
                        break;
                    case EVENT_ENRAGE:
                        me->TextEmote(EMOTE_ENRAGE, nullptr, true);
                        DoCast(me, SPELL_ENRAGE);
                        events.ScheduleEvent(EVENT_ENRAGE, 15000);
                        break;
                    case EVENT_DECIMATE:
                        me->TextEmote(EMOTE_DECIMATE, nullptr, true);
                        DoCastAOE(SPELL_DECIMATE);
                        for (SummonList::iterator itr = summons.begin(); itr != summons.end(); ++itr)
                            AttackGluthDec(*itr);
                        events.ScheduleEvent(EVENT_DECIMATE, 110000);
                        break;
                    case EVENT_BERSERK:
                        DoCast(me, SPELL_BERSERK);
                        events.ScheduleEvent(EVENT_BERSERK, 5*60000);
                        break;
                    case EVENT_SUMMON:
                        for (int32 i = 0; i < RAID_MODE(1, 2); ++i)
                            DoSummon(NPC_ZOMBIE, PosSummon[rand32() % RAID_MODE(1, 3)]);
                        events.ScheduleEvent(EVENT_SUMMON, 10000);
                        break;
                    case EVENT_EATEN:
                        if (Creature* zombie = me->FindNearestCreature(NPC_ZOMBIE, 7.0f))
                            EatZombie(zombie);
                        events.ScheduleEvent(EVENT_EATEN, 1000);
                        break;                   
                }
            }

            /*if (me->GetVictim() && me->EnsureVictim()->GetEntry() == NPC_ZOMBIE)
            {
                if (me->IsWithinMeleeRange(me->GetVictim()))
                {
                    me->Kill(me->GetVictim());
                    me->ModifyHealth(int32(me->CountPctFromMaxHealth(5)));
                }
            }
            else*/
            DoMeleeAttackIfReady();
        }
    };

};

void AddSC_boss_gluth()
{
    new boss_gluth();
}
