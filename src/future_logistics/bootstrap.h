/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 */
/** @file bootstrap.h Single-player Future Logistics new-game bootstrap. */
#ifndef FUTURE_LOGISTICS_BOOTSTRAP_H
#define FUTURE_LOGISTICS_BOOTSTRAP_H

/** Called exactly once from MakeNewGameDone, after the human company is created. */
void BootstrapFutureLogistics();

/**
 * Read-only connectivity and ownership diagnostic, also usable after save/reload.
 * @return Whether human ownership and all inspected vehicle routes are valid.
 */
bool InspectFutureLogistics();

#endif
