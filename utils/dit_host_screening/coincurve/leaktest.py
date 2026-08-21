import ctypes, sys
lib = ctypes.CDLL("./libditread.dylib")
lib.read_dit.restype = ctypes.c_ulong
from eth_account import Account
from coincurve import PrivateKey
acct = Account.from_key(bytes(range(1,33)))
pk = PrivateKey(bytes(range(1,33)))
tx = {"nonce":0,"gasPrice":20_000_000_000,"gas":21000,"to":"0x"+"11"*20,"value":10**18,"chainId":1}
print("  before             :", lib.read_dit())
pk.sign(b"x"*32, hasher=None)
print("  after coincurve sign:", lib.read_dit())
pk.sign_recoverable(b"x"*32, hasher=None)
print("  after sign_recover  :", lib.read_dit())
acct.sign_transaction(tx)
print("  after sign_transaction:", lib.read_dit())
