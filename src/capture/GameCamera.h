#pragma once

// ============================================================
// GameCamera
//
// Reads the active camera FOV from the Skyrim engine at capture
// time.  Used to derive an equivalent 35mm focal length for EXIF
// metadata so that panorama stitchers (PhotoFileMerge V2) can
// consume PrintScreen V4 screenshots without manual parameter
// entry.
//
// All functions are safe to call from any thread.  When the game
// state is unavailable they return 0.0 and the caller should fall
// back to a default or skip EXIF generation.
// ============================================================

namespace GameCamera {

    // Returns the current horizontal FOV in degrees.
    // Picks the correct field based on the active camera state:
    //   - First-person  -> PlayerCamera::firstPersonFOV
    //   - Third-person  -> PlayerCamera::worldFOV
    //   - Free / other  -> PlayerCamera::worldFOV (fallback)
    // Returns 0.0 if PlayerCamera is unavailable or the value is
    // outside a sane range (1..170 degrees).
    double GetHorizontalFOVDegrees();

    // Convert a horizontal FOV (degrees) to a 35mm-equivalent
    // focal length (millimetres).
    //
    //   focal35 = 18 / tan(hFOV / 2)
    //
    // This is independent of image resolution.  The stitcher
    // converts to pixels via:  f_px = W * focal35 / 36.
    // Returns 0.0 if fovDeg is invalid.
    double FocalLength35mmFromFOV(double fovDeg);

} // namespace GameCamera