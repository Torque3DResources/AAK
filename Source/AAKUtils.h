//-----------------------------------------------------------------------------
// Copyright (c) 2012 GarageGames, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#ifndef _AAKUTILS_H_
#define _AAKUTILS_H_
#include "math/mathUtils.h"
namespace AAKUtils
{   /// Returns yaw and pitch angles from a given vector.
   ///
   /// Angles are in RADIANS.
   ///
   /// Assumes north is (0.0, 1.0, 0.0), the degrees move upwards clockwise.
   ///
   /// The range of yaw is 0 - 2PI.  The range of pitch is -PI/2 - PI/2.
   ///
   /// <b>ASSUMES Z AXIS IS UP</b>
   void getAnglesFromVector(const VectorF& vec, F32& yawAng, F32& pitchAng);
}
#endif
