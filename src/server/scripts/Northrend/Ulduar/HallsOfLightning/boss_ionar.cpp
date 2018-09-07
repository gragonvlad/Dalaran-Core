/*
 * Copyright (C) 2008-2016 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2006-2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
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

/*
 * Comment: Timer check pending
 */

#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "halls_of_lightning.h"
#include "SpellInfo.h"

enum IonarSpells
{
    SPELL_BALL_LIGHTNING_N = 52780,
    SPELL_BALL_LIGHTNING_H = 59800,
    SPELL_STATIC_OVERLOAD_N = 52658,
    SPELL_STATIC_OVERLOAD_H = 59795,

    SPELL_DISPERSE = 52770,
    SPELL_SUMMON_SPARK = 52746,
    SPELL_SPARK_DESPAWN = 52776,

    //Spark of Ionar
    SPELL_SPARK_VISUAL_TRIGGER_N = 52667,
    SPELL_SPARK_VISUAL_TRIGGER_H = 59833,
    SPELL_RANDOM_LIGHTNING = 52663,
};

enum IonarOther
{
    // NPCs
    NPC_SPARK_OF_IONAR = 28926,

    // Actions
    ACTION_CALLBACK = 1,
    ACTION_SPARK_DESPAWN = 2,
};

enum Yells
{
    SAY_AGGRO = 0,
    SAY_SPLIT = 1,
    SAY_SLAY = 2,
    SAY_DEATH = 3
};

enum IonarEvents
{
    EVENT_BALL_LIGHTNING = 1,
    EVENT_STATIC_OVERLOAD = 2,
    EVENT_CHECK_HEALTH = 3,
    EVENT_CALL_SPARKS = 4,
    EVENT_RESTORE = 5,
    EVENT_UPDATE_TARGET = 5,
};

class boss_ionar : public CreatureScript
{
public:
    boss_ionar() : CreatureScript("boss_ionar") { }

    CreatureAI* GetAI(Creature* creature) const
    {
        return new boss_ionarAI(creature);
    }

    struct boss_ionarAI : public ScriptedAI
    {
        boss_ionarAI(Creature* creature) : ScriptedAI(creature), summons(creature)
        {
            m_pInstance = creature->GetInstanceScript();
        }

        InstanceScript* m_pInstance;
        EventMap events;
        SummonList summons;
        uint8 HealthCheck;
        bool hasDispersed;

        void Reset()
        {
            HealthCheck = 50;
            hasDispersed = false;
            events.Reset();
            summons.DespawnAll();

            me->SetVisible(true);

            if (m_pInstance)
                m_pInstance->SetBossState(DATA_IONAR, NOT_STARTED);

            // Ionar is immune to nature damage
            me->ApplySpellImmune(0, IMMUNITY_DAMAGE, SPELL_SCHOOL_MASK_NATURE, true);
        }

        void ScheduleEvents(bool spark)
        {
            events.SetPhase(1);
            if (!spark)
                events.RescheduleEvent(EVENT_CHECK_HEALTH, 1000, 0, 1);

            events.RescheduleEvent(EVENT_BALL_LIGHTNING, 10000, 0, 1);
            events.RescheduleEvent(EVENT_STATIC_OVERLOAD, 5000, 0, 1);
        }

        void EnterCombat(Unit*)
        {
            me->SetInCombatWithZone();
            Talk(SAY_AGGRO);

            if (m_pInstance)
                m_pInstance->SetBossState(DATA_IONAR, IN_PROGRESS);

            ScheduleEvents(false);
        }

        void JustDied(Unit*)
        {
            Talk(SAY_DEATH);

            summons.DespawnAll();

            if (m_pInstance)
                m_pInstance->SetBossState(DATA_IONAR, DONE);
        }

        void KilledUnit(Unit* victim)
        {
            if (victim->GetTypeId() != TYPEID_PLAYER)
                return;

            Talk(SAY_SLAY);
        }

        void SpellHit(Unit* /*caster*/, const SpellInfo* spell)
        {
            if (spell->Id == SPELL_DISPERSE)
                Split();
        }

        void Split()
        {
            Talk(SAY_SPLIT);
            hasDispersed = true;
            Creature* spark;
            for (uint8 i = 0; i < 5; ++i)
            {
                if (spark = me->SummonCreature(NPC_SPARK_OF_IONAR, me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0, TEMPSUMMON_TIMED_DESPAWN, 20000))
                {
                    summons.Summon(spark);
                    spark->CastSpell(spark, me->GetMap()->IsHeroic() ? SPELL_SPARK_VISUAL_TRIGGER_H : SPELL_SPARK_VISUAL_TRIGGER_N, true);
                    spark->CastSpell(spark, SPELL_RANDOM_LIGHTNING, true);
                    spark->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED | UNIT_FLAG_NOT_SELECTABLE | UNIT_FLAG_NON_ATTACKABLE);
                    spark->SetHomePosition(me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), 0);
                }
            }

            me->SetVisible(false);
            me->SetControlled(true, UNIT_STATE_STUNNED);

            events.SetPhase(2);
            events.ScheduleEvent(EVENT_CALL_SPARKS, 15000, 0, 2);
        }

        void UpdateAI(uint32 diff)
        {
            if (!UpdateVictim())
                return;

            events.Update(diff);

            if (me->HasUnitState(UNIT_STATE_CASTING))
                return;

            switch (events.ExecuteEvent())
            {
            case EVENT_BALL_LIGHTNING:
                if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM))
                    me->CastSpell(target, me->GetMap()->IsHeroic() ? SPELL_BALL_LIGHTNING_H : SPELL_BALL_LIGHTNING_N, false);

                events.RescheduleEvent(EVENT_BALL_LIGHTNING, 10000 + rand() % 1000);
                break;
            case EVENT_STATIC_OVERLOAD:
                if (Unit* target = SelectTarget(SELECT_TARGET_RANDOM))
                    me->CastSpell(target, me->GetMap()->IsHeroic() ? SPELL_STATIC_OVERLOAD_H : SPELL_STATIC_OVERLOAD_N, false);

                events.RescheduleEvent(EVENT_STATIC_OVERLOAD, 5000 + rand() % 1000);
                break;
            case EVENT_CHECK_HEALTH:
                if (HealthBelowPct(HealthCheck) && !hasDispersed)
                    me->CastSpell(me, SPELL_DISPERSE, false);

                events.RescheduleEvent(EVENT_CHECK_HEALTH, 1000);
                return;
            case EVENT_CALL_SPARKS:
            {
                EntryCheckPredicate pred(NPC_SPARK_OF_IONAR);
                summons.DoAction(ACTION_CALLBACK, pred);                
                events.ScheduleEvent(EVENT_RESTORE, 3000, 0, 2);
                return;
            }
            case EVENT_RESTORE:
                EntryCheckPredicate pred(NPC_SPARK_OF_IONAR);
                summons.DoAction(ACTION_SPARK_DESPAWN, pred);                

                me->SetVisible(true);
                me->SetControlled(false, UNIT_STATE_STUNNED);
                ScheduleEvents(true);
                return;
            }

            DoMeleeAttackIfReady();
        }
    };
};

/*######
## npc_spark_of_ionar
######*/

class npc_spark_of_ionar : public CreatureScript
{
public:
    npc_spark_of_ionar() : CreatureScript("npc_spark_of_ionar") { }

    CreatureAI* GetAI(Creature* creature) const
    {
        return new npc_spark_of_ionarAI(creature);
    }

    struct npc_spark_of_ionarAI : public ScriptedAI
    {
        npc_spark_of_ionarAI(Creature* creature) : ScriptedAI(creature) { instance = me->GetInstanceScript(); }

        bool _returning;
        EventMap _events;
        ObjectGuid _targetGUID;
        InstanceScript* instance;

        void MoveInLineOfSight(Unit*) { }
        void AttackStart(Unit* who) { }

        void Reset()
        {
            _returning = false;                         
            me->SetReactState(REACT_PASSIVE);
            if (Player* targetPlayer = SelectTargetFromPlayerList(100))
            {
                me->GetMotionMaster()->MovePoint(0, targetPlayer->GetPositionX(), targetPlayer->GetPositionY(), targetPlayer->GetPositionZ());
                _targetGUID = targetPlayer->GetGUID();
                _events.ScheduleEvent(EVENT_UPDATE_TARGET, 500);
            }             
        }

        void DamageTaken(Unit*, uint32 &damage, DamageEffectType, SpellSchoolMask)
        {
            damage = 0;
        }

        void DoAction(int32 param)
        {
            if (param == ACTION_CALLBACK)
            {
                me->SetSpeed(MOVE_RUN, 2.5f);
                me->DeleteThreatList();
                me->CombatStop(true);
                me->GetMotionMaster()->Clear();
                me->GetMotionMaster()->MoveIdle();
                if (Creature* ionar = ObjectAccessor::GetCreature(*me, instance->GetGuidData(DATA_IONAR)))
                    me->GetMotionMaster()->MovePoint(0, ionar->GetPositionX(), ionar->GetPositionY(), ionar->GetPositionZ());
                _events.CancelEvent(EVENT_UPDATE_TARGET);
                _returning = true;
            }
            else if (param == ACTION_SPARK_DESPAWN)
            {
                me->GetMotionMaster()->MoveIdle();
                me->RemoveAllAuras();
                me->CastSpell(me, SPELL_SPARK_DESPAWN, true);
                me->DespawnOrUnsummon(1000);
            }
        }

        void UpdateAI(uint32 diff)
        {
            if (_returning)
                return;
            _events.Update(diff);

            switch (_events.ExecuteEvent())
            {
            case EVENT_UPDATE_TARGET:
                if (_targetGUID)
                    if (Player* player = ObjectAccessor::GetPlayer(*me, _targetGUID))
                        me->GetMotionMaster()->MovePoint(0, player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
                me->SetSpeed(MOVE_WALK, 0.5f);
                me->SetSpeed(MOVE_RUN, 1.14286f);
                _events.RescheduleEvent(EVENT_UPDATE_TARGET, 500);
                break;
            default:
                break;
            }
        }
    };
};

void AddSC_boss_ionar()
{
    new boss_ionar();
    new npc_spark_of_ionar();
}