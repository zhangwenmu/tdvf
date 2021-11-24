import unittest
import requests
import socket
import time

HOST_IP = '10.239.92.53'
THREE_RD_HOST_PORT = '5001'
CLAMAV_HOST_PORT = '5002'
SDLE_HOST_PORT = '5003'
KLOCKWORK_HOST_PORT = '5004'
BDBA_HOST_PORT = '5005'


class TDVF_case(object):

    def __init__(self):
        self.username = 'wenmu'
        self.host = 'http://' + HOST_IP + ':'
        self.headers = {'Content-Type': 'application/json'}

    def test_scan(self, data):
        url = self.host + CLAMAV_HOST_PORT + '/rest/clamav/scan/' + self.username + '/'
        resp = requests.post(url, json=data, headers=self.headers, verify=False)
        return resp

    def test_submit(self, data):
        url = self.host + KLOCKWORK_HOST_PORT + '/rest/klocwork/submit/' + self.username + '/'
        resp = requests.post(url, json=data, headers=self.headers, verify=False)
        return resp


class Clamav_test(unittest.TestCase):
    def test_clamav(self):
        suits = [
            {
                "project": "zhangwenmu",
                "components": [
                    {
                        "name": "tdvf",
                        "type": "code",
                        "branch": "master",
                        "url": "https://github.com/zhangwenmu/tdvf",
                    }
                ]
            }
        ]
        cc = TDVF_case()
        for suit in suits:
            resp = cc.test_scan(suit)
            self.assertEqual(resp.status_code, 200)
            time.sleep(3)


class KW_test(unittest.TestCase):

    def test_kw(self):
        suites = [
            {
                "project": "zhangwenmu",
                "component": "tdvf",
                "url": "https://github.com/zhangwenmu/tdvf",
                "kw_scan_link": "http://{}:8000/example.html".format(HOST_IP)
            }
        ]
        kt = TDVF_case()
        for suite in suites:
            resp = kt.test_submit(suite)
            self.assertEqual(resp.status_code, 200)
            time.sleep(3)



if __name__ == '__main__':
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect(("8.8.8.8", 80))
    unittest.main()
    s.close()
