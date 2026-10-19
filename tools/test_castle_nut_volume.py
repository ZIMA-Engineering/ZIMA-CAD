"""Calibrate independent section integration against elementary exact areas."""
import importlib.util
import math
from pathlib import Path
import unittest

spec=importlib.util.spec_from_file_location('sections',Path(__file__).with_name('castle-nut-volume.py'))
sections=importlib.util.module_from_spec(spec);spec.loader.exec_module(sections)


class SectionAreas(unittest.TestCase):
    def test_inscribed_disk_tangencies(self):
        for r in (.25,1,8.5,40):
            square=[(-r,-r),(r,-r),(r,r),(-r,r)]
            self.assertAlmostEqual(sections.disk_polygon(square,r),math.pi*r*r,places=9)

    def test_half_disk(self):
        self.assertAlmostEqual(sections.disk_polygon([(0,-2),(2,-2),(2,2),(0,2)],1),math.pi/2,places=12)

    def test_contained_polygon(self):
        self.assertAlmostEqual(sections.disk_polygon([(-1,-1),(1,-1),(1,1),(-1,1)],3),4,places=12)

    def test_strip_disk(self):
        square=[(-3,-3),(3,-3),(3,3),(-3,3)]
        for width in (.1,.5,.99):
            poly=sections.clip(sections.clip(square,1,0,width),-1,0,width)
            expected=2*(width*math.sqrt(1-width*width)+math.asin(width))
            self.assertAlmostEqual(sections.disk_polygon(poly,1),expected,places=12)

    def test_hexagon(self):
        poly=[(math.cos(i*math.pi/3),math.sin(i*math.pi/3)) for i in range(6)]
        self.assertAlmostEqual(sections.disk_polygon(poly,1),3*math.sqrt(3)/2,places=12)

    def test_integral(self):
        self.assertAlmostEqual(sections.integral(lambda x:math.sin(x),0,math.pi,1e-11),2,places=11)


if __name__=='__main__':unittest.main()
