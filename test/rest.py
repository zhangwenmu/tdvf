# import json
# import requests
#
# # 创建Rest类
# class Rest:
#     # 定义类属性
#     base_url = ''
#     user = ''
#     pw = ''
#
#     def __init__(self, base_url, user='', pw=''):
#         self.base_url = base_url
#         self.user = user
#         self.pw = pw
#
#     def post(self, endpoint, json_data=None):
#         headers = {'Content-Type': 'application/json'}
#         rest_url = self.base_url + endpoint
#         auth = None
#         json_str = json.dumps(json_data)
#         if self.user != '':
#             auth = (self.user, self.pw)
#
#         response = requests.post(rest_url, json_str, headers=headers, verify=False, auth=auth)  # verify=False移除SSL认证
#         # 获取响应的状态码
#         status_code = response.status_code
#         # 获取 bytes 类型并解码成 str
#         content = response.content.decode()
#         if status_code != 200:
#             raise ValueError('%d: %s' % (status_code, content))
#         # 返回成dict 类型
#         return json.loads(content)
#
#     def get(self, endpoint):
#         headers = {'Content-Type': 'application/json'}
#         rest_url = self.base_url + endpoint
#         auth = None
#         if self.user != '':
#             auth = (self.user, self.pw)
#
#         response = requests.get(rest_url, None, headers=headers, verify=False, auth=auth)
#         status_code = response.status_code
#         content = response.content.decode()
#         if status_code != 200:
#             raise ValueError('%d: %s' % (status_code, content))
#         return json.loads(content)
#
#     pass
