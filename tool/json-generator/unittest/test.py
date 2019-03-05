#!/usr/bin/python3
# coding: utf-8
import unittest
import jsonGenerator

class jsonGeneratorTestCase(unittest.TestCase):
    def setUp(self):
        # If user need to visit http://127.0.0.1/nc/v1/welcome?name=John
        url_postfix = "/nc/v1/welcome?name=John"
        self.creater = jsonGenerator.jsonGenerator()

    def test_json_generator(self):
        # METHOD is GET
        jsonContent = self.creater.jsonByGetFile("test/get.txt")
        self.assertIsNotNone(jsonContent)
    
        # METHOD is POST
        dataFile = "test/data.txt"
        jsonContent = self.creater.jsonByPostFile("test/post_char.txt", "test/url.txt")
        self.assertIsNotNone(jsonContent)

        jsonContent = self.creater.jsonByPostFile("test/post_char.txt", "test/4urls.txt")
        self.assertIsNone(jsonContent)

        jsonContent = self.creater.jsonByPostFile("test/post_char.txt", "test/2urls.txt")
        self.assertIsNotNone(jsonContent)

        # METHOD is POST base64
        jsonContent = self.creater.jsonByBase64Dir("test/post_base64", "test/url.txt")
        self.assertIsNotNone(jsonContent)

        jsonContent = self.creater.jsonByBase64Dir("test/post_base64", "test/4urls.txt")
        self.assertIsNone(jsonContent)

        jsonContent = self.creater.jsonByBase64Dir("test/post_base64", "test/2urls.txt")
        self.assertIsNotNone(jsonContent)

if __name__ == "__main__":
    unittest.main()
