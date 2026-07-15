void IconFightScene::UpdateCombatDirector(double deltaSeconds, double actionDeltaSeconds)
{
    const CombatDirectorBounds bounds{
        static_cast<double>(wanderBounds_.left), static_cast<double>(wanderBounds_.top),
        static_cast<double>(wanderBounds_.right), static_cast<double>(wanderBounds_.bottom)};
    const EncounterBounds encounterBounds{bounds.left, bounds.top, bounds.right, bounds.bottom};
    UpdateActiveEncounterPoolClock(
        activeEncounterPool_, actionDeltaSeconds,
        IsFirstWaveEcosystemReady(awakeningDirectorState_, awakeningObservations_));

    ecosystemPerceptionInputs_.clear();
    ecosystemRuntimeSnapshots_.clear();
    ecosystemIntentSnapshot_.clear();
    encounterArbiterActors_.clear();
    activeEncounterActorInputs_.clear();
    for (std::size_t index = 0; index < actors_.size(); ++index) {
        IconActor& actor = actors_[index];
        UpdateActorRuntimeState(actor.runtimeState, actionDeltaSeconds);
        const bool awake = elapsedSeconds_ - actor.awakeningStartSeconds >=
            kAwakeningDurationSeconds + kLimbGrowthDurationSeconds;
        const bool controlled = actor.combatPreviewActor ||
            ActiveEncounterPoolOwnsActor(activeEncounterPool_, index);
        const bool actionActive = actor.actionPlayer.State().actionId != ActionId::None;
        const bool recovering = actor.pendingCombatAction != ActionId::None ||
            actor.combatBlendDuration > 0.0;
        const bool wandering = awake && !actor.actionPreviewActor && !actor.turnPreviewActor &&
            !actor.combatPreviewActor;
        double velocityX = 0.0;
        double velocityY = 0.0;
        const double dx = actor.targetX - actor.x;
        const double dy = actor.targetY - actor.y;
        const double distance = std::hypot(dx, dy);
        if (wandering && !actor.turnMotion.turning && !actionActive && !recovering &&
            actor.waitRemaining <= 0.0 && distance > 1e-6) {
            velocityX = dx / distance * actor.walkSpeed;
            velocityY = dy / distance * actor.walkSpeed;
        }
        ecosystemPerceptionInputs_.push_back({
            index, actor.x, actor.y, velocityX, velocityY,
            actor.turnMotion.currentFacing == TurnFacing::Right ? 1.0 : -1.0,
            0.0, std::max(actor.planeWidth, actor.planeHeight),
            awake, wandering, actor.turnMotion.turning, actionActive, recovering, controlled});
        ecosystemIntentSnapshot_.push_back(actor.runtimeState.heldIntent);
        encounterArbiterActors_.push_back({index, controlled});
        activeEncounterActorInputs_.push_back({
            index, actor.x, actor.y, std::max(actor.planeWidth, actor.planeHeight),
            ActorAssessDurationAdjustment(actor.behaviorProfile.tendency)});
    }

    const bool mayForm = ActiveEncounterPoolMayAccept(activeEncounterPool_);
    for (const IconActor& actor : actors_) ecosystemRuntimeSnapshots_.push_back(actor.runtimeState);
    if (mayForm) {
        for (std::size_t index = 0; index < actors_.size(); ++index) {
            const LocalPerception perception = FindLocalPerception(
                index, ecosystemPerceptionInputs_, ecosystemRuntimeSnapshots_, encounterBounds);
            const LocalIntent observed =
                perception.perceived && perception.actorIndex < ecosystemIntentSnapshot_.size() ?
                ecosystemIntentSnapshot_[perception.actorIndex] : LocalIntent::Ignore;
            UpdateActorLocalIntent(
                actors_[index].runtimeState, actors_[index].behaviorProfile,
                perception, observed, actionDeltaSeconds);
        }
        ecosystemRuntimeSnapshots_.clear();
        for (const IconActor& actor : actors_) ecosystemRuntimeSnapshots_.push_back(actor.runtimeState);
    }

    const std::vector<LocalEncounterRequest> requests = BuildLocalEncounterRequests(
        ecosystemPerceptionInputs_, ecosystemRuntimeSnapshots_, mayForm);
    const std::vector<EncounterReservation> reservations =
        ActiveEncounterReservations(activeEncounterPool_);
    const EncounterArbitrationResult arbitration = ArbitrateEncounterRequests(
        requests, encounterArbiterActors_, encounterBounds, reservations);
    RecordActiveEncounterArbitrationRejections(activeEncounterPool_, arbitration.rejected);
    const ActiveEncounterPoolStartResult starts = StartAcceptedEncounters(
        activeEncounterPool_, arbitration.accepted, activeEncounterActorInputs_, encounterBounds);
    for (const std::uint64_t id : starts.startedIds) {
        ActiveEncounter* encounter = FindActiveEncounter(activeEncounterPool_, id);
        if (encounter == nullptr) continue;
        for (const std::size_t index : {encounter->attackerIndex, encounter->defenderIndex}) {
            actors_[index].combatPreviewActor = true;
            actors_[index].combatImpactVisible = false;
            actors_[index].combatBlockedImpact = false;
        }
        LogInfo(
            L"active encounter started: id=" + std::to_wstring(id) +
            L"; actors=" + std::to_wstring(encounter->attackerIndex) + L"," +
            std::to_wstring(encounter->defenderIndex) + L"; scenario=" +
            std::wstring(CombatScenarioIdName(encounter->scenario)) + L"; intent=" +
            std::wstring(EncounterIntentName(encounter->encounter.intent)));
    }

    const auto clearActor = [](IconActor& actor) {
        actor.combatPreviewActor = false;
        actor.combatImpactVisible = false;
        actor.combatBlockedImpact = false;
        actor.actionPlayer.Stop();
        actor.actionSample = {};
        actor.encounterPose = {};
        actor.pendingCombatAction = ActionId::None;
        actor.combatBlendDuration = 0.0;
        actor.baseX = actor.x;
        actor.baseY = actor.y;
        actor.waitRemaining = 0.0;
    };
    const auto distanceTo = [](const IconActor& actor, double x, double y) {
        return std::hypot(x - actor.x, y - actor.y);
    };
    const auto blendLocomotion = [deltaSeconds](IconActor& actor, double target) {
        actor.locomotionWeight = BlendTurnLocomotion(actor.locomotionWeight, target, deltaSeconds);
    };
    const auto advanceGait = [](IconActor& actor, double distance) {
        const double side = std::max(24.0, std::max(actor.planeWidth, actor.planeHeight));
        const GaitGeometry geometry = BuildGaitGeometry(side, side * 0.40, side * 0.41);
        actor.walkPhase = WrapGaitPhase(actor.walkPhase + distance / geometry.cycleTravel);
    };

    for (ActiveEncounter& active : activeEncounterPool_.encounters) {
        if (active.released) continue;
        const std::uint64_t id = active.id;
        const std::size_t attackerIndex = active.attackerIndex;
        const std::size_t defenderIndex = active.defenderIndex;
        if (attackerIndex >= actors_.size() || defenderIndex >= actors_.size() ||
            attackerIndex == defenderIndex) {
            ReleaseActiveEncounter(activeEncounterPool_, id, true);
            LogWarning(L"active encounter cancelled because actor indices became invalid");
            continue;
        }
        IconActor& attacker = actors_[attackerIndex];
        IconActor& defender = actors_[defenderIndex];
        const auto awake = [this](const IconActor& actor) {
            return elapsedSeconds_ - actor.awakeningStartSeconds >=
                kAwakeningDurationSeconds + kLimbGrowthDurationSeconds;
        };
        const bool actorsValid = awake(attacker) && awake(defender) &&
            std::isfinite(attacker.x) && std::isfinite(attacker.y) &&
            std::isfinite(defender.x) && std::isfinite(defender.y);
        const bool reservationSafe =
            active.reservation.centerX - active.reservation.radius >= bounds.left &&
            active.reservation.centerX + active.reservation.radius <= bounds.right &&
            active.reservation.centerY - active.reservation.radius >= bounds.top &&
            active.reservation.centerY + active.reservation.radius <= bounds.bottom;
        const bool atStations =
            distanceTo(attacker, active.stationLeftX, active.stationY) <= 1.5 &&
            distanceTo(defender, active.stationRightX, active.stationY) <= 1.5;
        const bool aligned = atStations &&
            !attacker.turnMotion.turning && !defender.turnMotion.turning &&
            attacker.turnMotion.currentFacing == TurnFacing::Right &&
            defender.turnMotion.currentFacing == TurnFacing::Left;
        const bool combatComplete = active.encounter.phase == EncounterPhase::Combat &&
            active.combatPair.phase == CombatPairPhase::Returning &&
            active.combatPair.result != CombatResult::None &&
            attacker.actionPlayer.State().actionId == ActionId::None &&
            defender.actionPlayer.State().actionId == ActionId::None &&
            attacker.pendingCombatAction == ActionId::None &&
            defender.pendingCombatAction == ActionId::None;
        const bool separated = active.encounter.phase == EncounterPhase::Separating &&
            distanceTo(attacker, active.encounter.aftermath.attackerExit.x,
                active.encounter.aftermath.attackerExit.y) <= 1.5 &&
            distanceTo(defender, active.encounter.aftermath.defenderExit.x,
                active.encounter.aftermath.defenderExit.y) <= 1.5;
        const EncounterStep step = UpdateEncounter(
            active.encounter,
            {actorsValid, reservationSafe, atStations, aligned, combatComplete,
                separated, active.combatPair.result},
            actionDeltaSeconds);

        if (step.cancelled) {
            clearActor(attacker);
            clearActor(defender);
            ReleaseActiveEncounter(activeEncounterPool_, id, true);
            ChooseWanderTarget(attacker);
            ChooseWanderTarget(defender);
            LogWarning(L"active encounter cancelled safely: id=" + std::to_wstring(id));
            continue;
        }

        attacker.encounterPose = SampleEncounterPose(active.encounter, EncounterActorRole::Attacker);
        defender.encounterPose = SampleEncounterPose(active.encounter, EncounterActorRole::Defender);
        const auto moveTo = [&](IconActor& actor, double targetX, double targetY,
                                bool slowDown, bool preserveFacing) {
            const double dx = targetX - actor.x;
            const double dy = targetY - actor.y;
            const double distance = std::hypot(dx, dy);
            if (distance <= 1.5) {
                actor.x = targetX;
                actor.y = targetY;
                blendLocomotion(actor, 0.0);
                return;
            }
            if (!preserveFacing) {
                const TurnFacing desired = FacingFromDirection(dx);
                if (actor.turnMotion.turning) {
                    blendLocomotion(actor, 0.0);
                    if (actor.locomotionWeight <= 0.02) {
                        UpdateTurnMotion(actor.turnMotion, actionDeltaSeconds);
                    }
                    return;
                }
                if (RequestTurn(actor.turnMotion, desired)) {
                    blendLocomotion(actor, 0.0);
                    return;
                }
            }
            const double side = std::max(24.0, std::max(actor.planeWidth, actor.planeHeight));
            const double weight = slowDown ?
                std::clamp(distance / (side * 1.8), 0.12, 1.0) : 1.0;
            blendLocomotion(actor, weight);
            const double speedScale = preserveFacing ? 0.48 :
                (slowDown ? std::max(0.32, weight) : 1.0);
            const double move = std::min(distance, actor.walkSpeed * speedScale * deltaSeconds);
            actor.x += dx / distance * move;
            actor.y += dy / distance * move;
            advanceGait(actor, move);
        };
        const auto faceEachOther = [&]() {
            blendLocomotion(attacker, 0.0);
            blendLocomotion(defender, 0.0);
            if (attacker.locomotionWeight <= 0.02) {
                if (!attacker.turnMotion.turning) RequestTurn(attacker.turnMotion, TurnFacing::Right);
                UpdateTurnMotion(attacker.turnMotion, actionDeltaSeconds);
            }
            if (defender.locomotionWeight <= 0.02) {
                if (!defender.turnMotion.turning) RequestTurn(defender.turnMotion, TurnFacing::Left);
                UpdateTurnMotion(defender.turnMotion, actionDeltaSeconds);
            }
        };

        switch (active.encounter.phase) {
        case EncounterPhase::Approaching:
            moveTo(attacker, active.stationLeftX, active.stationY, true, false);
            moveTo(defender, active.stationRightX, active.stationY, true, false);
            break;
        case EncounterPhase::Facing:
            faceEachOther();
            break;
        case EncounterPhase::Intent:
            if (active.encounter.intent != EncounterIntent::Combat) {
                moveTo(attacker, active.encounter.attackerIntentTarget.x,
                    active.encounter.attackerIntentTarget.y, true, true);
                moveTo(defender, active.encounter.defenderIntentTarget.x,
                    active.encounter.defenderIntentTarget.y, true, true);
            }
            break;
        case EncounterPhase::Combat:
            if (step.requestCombatStart) {
                active.combatPair = {};
                active.loggedCombatPhase = CombatPairPhase::Inactive;
            }
            UpdateCombatPairActors(
                attackerIndex, defenderIndex, active.scenario, true,
                active.combatPair, active.stationLeftX, active.stationRightX,
                active.stationY, active.loggedCombatPhase, deltaSeconds, actionDeltaSeconds);
            break;
        case EncounterPhase::Separating:
            if (EncounterActorMayDepart(active.encounter, EncounterActorRole::Attacker)) {
                moveTo(attacker, active.encounter.aftermath.attackerExit.x,
                    active.encounter.aftermath.attackerExit.y, false, false);
            } else blendLocomotion(attacker, 0.0);
            if (EncounterActorMayDepart(active.encounter, EncounterActorRole::Defender)) {
                moveTo(defender, active.encounter.aftermath.defenderExit.x,
                    active.encounter.aftermath.defenderExit.y, false, false);
            } else blendLocomotion(defender, 0.0);
            break;
        default:
            blendLocomotion(attacker, 0.0);
            blendLocomotion(defender, 0.0);
            break;
        }

        if (active.encounter.phase != active.loggedEncounterPhase) {
            active.loggedEncounterPhase = active.encounter.phase;
            LogInfo(L"active encounter phase: id=" + std::to_wstring(id) +
                L"; phase=" + std::wstring(EncounterPhaseName(active.loggedEncounterPhase)) +
                L"; intent=" + std::wstring(EncounterIntentName(active.encounter.intent)));
        }
        if (!step.completed) continue;

        ActorEncounterOutcome attackerOutcome = ActorEncounterOutcome::Bluff;
        ActorEncounterOutcome defenderOutcome = ActorEncounterOutcome::Bluff;
        if (active.encounter.intent == EncounterIntent::Combat) {
            attackerOutcome = ActorEncounterOutcome::Combat;
            defenderOutcome = ActorEncounterOutcome::Combat;
        } else if (active.encounter.intent == EncounterIntent::Yield) {
            const bool attackerYielded = active.encounter.attackerActsFirst;
            attackerOutcome = attackerYielded ?
                ActorEncounterOutcome::Yielded : ActorEncounterOutcome::CounterpartYielded;
            defenderOutcome = attackerYielded ?
                ActorEncounterOutcome::CounterpartYielded : ActorEncounterOutcome::Yielded;
        }
        const CombatDirectorTuning& tuning = GetCombatDirectorTuning();
        const double cooldown = tuning.actorCooldownSeconds +
            (actors_.size() <= 6 ? tuning.sparseActorCooldownBonusSeconds : 0.0);
        RecordActorEncounter(attacker.runtimeState, defenderIndex, attackerOutcome, cooldown);
        RecordActorEncounter(defender.runtimeState, attackerIndex, defenderOutcome, cooldown);
        clearActor(attacker);
        clearActor(defender);
        ReleaseActiveEncounter(activeEncounterPool_, id, false);
        ChooseWanderTargetAwayFrom(attacker, defender.x, -1.0);
        ChooseWanderTargetAwayFrom(defender, attacker.x, 1.0);
        LogInfo(L"active encounter completed: id=" + std::to_wstring(id));
    }

    CleanupReleasedEncounters(activeEncounterPool_);
    if (loggedActiveEncounterCount_ != activeEncounterPool_.encounters.size()) {
        loggedActiveEncounterCount_ = activeEncounterPool_.encounters.size();
        LogInfo(L"active encounter pool: active=" +
            std::to_wstring(loggedActiveEncounterCount_) + L"; accepted=" +
            std::to_wstring(activeEncounterPool_.stats.acceptedRequestCount) +
            L"; occupied rejected=" +
            std::to_wstring(activeEncounterPool_.stats.occupiedRejectionCount) +
            L"; reservation rejected=" +
            std::to_wstring(activeEncounterPool_.stats.reservationRejectionCount) +
            L"; completed=" +
            std::to_wstring(activeEncounterPool_.stats.completedEncounterCount) +
            L"; abnormal releases=" +
            std::to_wstring(activeEncounterPool_.stats.abnormalReleaseCount));
    }
}
