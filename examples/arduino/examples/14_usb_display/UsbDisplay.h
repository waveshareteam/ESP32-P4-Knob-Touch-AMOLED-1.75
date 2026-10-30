/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

class UsbDisplay {
public:
    bool begin();
    void tick();
    const char *lastError() const;
};
