// This file is part of Drawpile.
// Copyright (C) 2020 Calle Laakkonen
//
// Drawpile is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// As additional permission under section 7, you are allowed to distribute
// the software through an app store, even if that store has restrictive
// terms and conditions that are incompatible with the GPL, provided that
// the source is also available under the GPL with or without this permission
// through a channel without those restrictive terms and conditions.
//
// Drawpile is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Drawpile.  If not, see <https://www.gnu.org/licenses/>.

use std::fmt;
use std::str::FromStr;

pub type Pixel = [u8; 4];

pub const BLUE_CHANNEL: usize = 0;
pub const GREEN_CHANNEL: usize = 1;
pub const RED_CHANNEL: usize = 2;
pub const ALPHA_CHANNEL: usize = 3;

pub const RGB_CHANNELS: std::ops::RangeInclusive<usize> = 0..=2;
pub const ZERO_PIXEL: Pixel = [0, 0, 0, 0];
pub const WHITE_PIXEL: Pixel = [255, 255, 255, 255];

#[derive(Copy, Clone, Debug)]
#[repr(C)]
pub struct Color {
    pub r: f32,
    pub g: f32,
    pub b: f32,
    pub a: f32,
}

impl Color {
    pub const TRANSPARENT: Self = Self {
        r: 0.0,
        g: 0.0,
        b: 0.0,
        a: 0.0,
    };

    pub const BLACK: Self = Self {
        r: 0.0,
        g: 0.0,
        b: 0.0,
        a: 1.0,
    };

    pub const WHITE: Self = Self {
        r: 1.0,
        g: 1.0,
        b: 1.0,
        a: 1.0,
    };

    pub fn rgb8(r: u8, g: u8, b: u8) -> Color {
        Color {
            r: r as f32 / 255.0,
            g: g as f32 / 255.0,
            b: b as f32 / 255.0,
            a: 1.0,
        }
    }

    pub fn from_argb32(c: u32) -> Color {
        Color {
            r: ((c & 0x00_ff0000) >> 16) as f32 / 255.0,
            g: ((c & 0x00_00ff00) >> 8) as f32 / 255.0,
            b: (c & 0x00_0000ff) as f32 / 255.0,
            a: ((c & 0xff_000000) >> 24) as f32 / 255.0,
        }
    }

    pub fn from_hsv(h: f32, s: f32, v: f32) -> Color {
        let c = v * s;
        let hp = (h / 60.0) % 6.0;
        let x = c * (1.0 - ((hp % 2.0) - 1.0).abs());
        let m = v - c;

        let r;
        let g;
        let b;
        if 0.0 <= hp && hp < 1.0 {
            r = c;
            g = x;
            b = 0.0;
        } else if hp < 2.0 {
            r = x;
            g = c;
            b = 0.0;
        } else if hp < 3.0 {
            r = 0.0;
            g = c;
            b = x;
        } else if hp < 4.0 {
            r = 0.0;
            g = x;
            b = c;
        } else if hp < 5.0 {
            r = x;
            g = 0.0;
            b = c;
        } else if hp < 6.0 {
            r = c;
            g = 0.0;
            b = x;
        } else {
            r = 0.0;
            g = 0.0;
            b = 0.0;
        }
        Color {
            r: r + m,
            g: g + m,
            b: b + m,
            a: 1.0,
        }
    }

    pub fn argb32_alpha(c: u32) -> u8 {
        ((c & 0xff_000000) >> 24) as u8
    }

    // Get a non-premultiplied pixel value from this color
    pub fn as_argb32(&self) -> u32 {
        ((self.r * 255.0) as u32) << 16
            | ((self.g * 255.0) as u32) << 8
            | ((self.b * 255.0) as u32)
            | ((self.a * 255.0) as u32) << 24
    }

    // Get a color from a premultiplied pixel value
    pub fn from_pixel(p: Pixel) -> Color {
        if p[ALPHA_CHANNEL] == 0 {
            return Color::TRANSPARENT;
        }
        let p = unpremultiply_pixel(p);

        Color {
            r: p[RED_CHANNEL] as f32 / 255.0,
            g: p[GREEN_CHANNEL] as f32 / 255.0,
            b: p[BLUE_CHANNEL] as f32 / 255.0,
            a: p[ALPHA_CHANNEL] as f32 / 255.0,
        }
    }

    // Get the color values as is, without unpremultiplying
    pub fn from_unpremultiplied_pixel(p: Pixel) -> Color {
        Color {
            r: p[RED_CHANNEL] as f32 / 255.0,
            g: p[GREEN_CHANNEL] as f32 / 255.0,
            b: p[BLUE_CHANNEL] as f32 / 255.0,
            a: p[ALPHA_CHANNEL] as f32 / 255.0,
        }
    }

    // Get a premultiplied pixel value from this color
    pub fn as_pixel(&self) -> Pixel {
        premultiply_pixel(self.as_unpremultiplied_pixel())
    }

    // Get a non-premultiplied pixel value from this color
    pub fn as_unpremultiplied_pixel(&self) -> Pixel {
        [
            (self.b * 255.0) as u8,
            (self.g * 255.0) as u8,
            (self.r * 255.0) as u8,
            (self.a * 255.0) as u8,
        ]
    }

    pub fn is_transparent(&self) -> bool {
        self.a < (1.0 / 255.0)
    }

    /// Is this a perceptually dark color
    pub fn is_dark(&self) -> bool {
        let luminance = self.r * 0.216 + self.g * 0.7152 + self.b * 0.0722;
        luminance <= 0.5
    }
}

impl fmt::Display for Color {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.a < 1.0 {
            write!(f, "#{:08x}", self.as_argb32())
        } else {
            write!(f, "#{:06x}", self.as_argb32() & 0x00_ffffff)
        }
    }
}

impl PartialEq for Color {
    fn eq(&self, other: &Self) -> bool {
        self.as_argb32() == other.as_argb32()
    }
}

impl FromStr for Color {
    type Err = &'static str;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        if s.is_empty() {
            return Err("empty color string");
        }
        if !s.starts_with('#') || (s.len() != 7 && s.len() != 9) {
            return Err("doesn't look like a color string");
        }

        if let Ok(v) = u32::from_str_radix(&s[1..], 16) {
            Ok(if s.len() == 7 {
                Color::from_argb32(v | 0xff_000000)
            } else {
                Color::from_argb32(v)
            })
        } else {
            Err("not a valid color")
        }
    }
}

pub fn unpremultiply_pixel(p: Pixel) -> Pixel {
    if p[ALPHA_CHANNEL] == 255 {
        return p;
    } else if p[ALPHA_CHANNEL] == 0 {
        return ZERO_PIXEL;
    }

    let ia = 0xff00ff / p[ALPHA_CHANNEL] as i32;
    [
        ((p[0] as i32 * ia + 0x8000) >> 16) as u8,
        ((p[1] as i32 * ia + 0x8000) >> 16) as u8,
        ((p[2] as i32 * ia + 0x8000) >> 16) as u8,
        p[3],
    ]
}

pub fn premultiply_pixel(p: Pixel) -> Pixel {
    if p[ALPHA_CHANNEL] == 255 {
        return p;
    } else if p[ALPHA_CHANNEL] == 0 {
        return ZERO_PIXEL;
    }
    fn mult(a: u32, b: u32) -> u32 {
        let c = a * b + 0x80;
        ((c >> 8) + c) >> 8
    }
    let a = p[ALPHA_CHANNEL] as u32;
    [
        mult(p[0] as u32, a) as u8,
        mult(p[1] as u32, a) as u8,
        mult(p[2] as u32, a) as u8,
        p[3],
    ]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_equality() {
        let c1 = Color::rgb8(0, 0, 0);
        let c2 = Color::rgb8(255, 255, 255);
        let c3 = Color::rgb8(255, 255, 254);

        assert!(c1 == c1);
        assert!(c1 != c2);
        assert!(c1 != c3);
        assert!(c2 != c3);
        assert!(
            c1 == Color {
                r: 0.001,
                g: 0.0,
                b: 0.0,
                a: 1.0
            }
        );
    }

    #[test]
    fn test_string_parsing() {
        assert_eq!(Color::TRANSPARENT, Color::from_str("#00000000").unwrap());
        assert_eq!(Color::rgb8(0, 0, 0), Color::from_str("#000000").unwrap());
        assert_eq!(Color::rgb8(255, 0, 0), Color::from_str("#ff0000").unwrap());
        assert_eq!(
            Color {
                r: 1.0,
                g: 0.0,
                b: 0.0,
                a: 0.49804
            },
            Color::from_str("#7fff0000").unwrap()
        );
    }

    #[test]
    fn test_premultiplication() {
        for i in 1..=255 {
            let p: Pixel = [i, i, i, i];

            let up = unpremultiply_pixel(p);
            assert_eq!(up, [255, 255, 255, i]);
            let p2 = premultiply_pixel(up);

            assert_eq!(p, p2);
        }
    }
}
