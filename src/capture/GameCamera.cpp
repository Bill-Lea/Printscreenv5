#include "PCH.h"
#include "GameCamera.h"

// ============================================================
// GameCamera implementation
//
// Skyrim's PlayerCamera stores two FOV fields (in degrees):
//   worldFOV         (0x13C) — third-person / world view
//   firstPersonFOV   (0x140) — first-person view
//
// The console "fov" command updates both.  INI settings
// fDefaultWorldFOV / fDefault1stPersonFOV seed them at load.
// We pick whichever matches the active camera state.
//
// Note: These fields are in DEGREES, not radians.  The NiCamera
// node's frustum may also carry FOV info in radians, but the
// PlayerCamera struct fields are the canonical, user-facing values
// and are what the "fov" console command sets.
// ============================================================

#include <cmath>

double GameCamera::GetHorizontalFOVDegrees()
{
    auto* cam = RE::PlayerCamera::GetSingleton();
    if (!cam) return 0.0;

    double fov = 0.0;

    // Choose the FOV field that matches the active camera state.
    if (cam->IsInFirstPerson()) {
        fov = static_cast<double>(cam->firstPersonFOV);
    } else {
        fov = static_cast<double>(cam->worldFOV);
    }

    // Sanity-check: Skyrim's FOV slider runs ~50..120, console allows
    // wider.  Reject anything implausible.
    if (fov < 1.0 || fov > 170.0) {
        // Fall back to the other field.
        if (cam->IsInFirstPerson())
            fov = static_cast<double>(cam->worldFOV);
        else
            fov = static_cast<double>(cam->firstPersonFOV);

        if (fov < 1.0 || fov > 170.0)
            return 0.0;  // both fields invalid
    }

    return fov;
}

double GameCamera::FocalLength35mmFromFOV(double fovDeg)
{
    if (fovDeg <= 0.0 || fovDeg >= 180.0) return 0.0;

    // 35mm horizontal sensor width = 36mm.
    // Pinhole model:  focal35 = (sensorWidth / 2) / tan(hFOV / 2)
    //                      = 18 / tan(hFOV / 2)
    constexpr double kSensorHalfWidth_mm = 18.0;
    const double halfFOVRad = fovDeg * 0.5 * 3.14159265358979323846 / 180.0;
    return kSensorHalfWidth_mm / std::tan(halfFOVRad);
}