// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2015-2018 The Blocknet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"
#include "clientversion.h"
#include "init.h"
#include "main.h"
#include "servicenode-sync.h"
#include "net.h"
#include "netbase.h"
#include "rpcserver.h"
#include "spork.h"
#include "timedata.h"
#include "util.h"

#include "xbridge/version.h"
#include "xrouter/version.h"
#include "xrouter/xrouterapp.h"

#ifdef ENABLE_WALLET
#include "currencypair.h"
#include "wallet.h"
#include "walletdb.h"
#endif

#include <stdint.h>

#include "json/json_spirit_utils.h"
#include "json/json_spirit_value.h"
#include <boost/assign/list_of.hpp>

using namespace boost;
using namespace boost::assign;
using namespace json_spirit;
using namespace std;

extern CurrencyPair TxOutToCurrencyPair(const std::vector<CTxOut> & vout, std::string& snode_pubkey);

/**
 * @note Do not add or change anything in the information returned by this
 * method. `getinfo` exists for backwards-compatibility only. It combines
 * information from wildly different sources in the program, which is a mess,
 * and is thus planned to be deprecated eventually.
 *
 * Based on the source of the information, new information should be added to:
 * - `getblockchaininfo`,
 * - `getnetworkinfo` or
 * - `getwalletinfo`
 *
 * Or alternatively, create a specific query method for the information.
 **/
Value getinfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getinfo\n"
            "Returns an object containing various state info.\n"
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,           (numeric) the server version\n"
            "  \"protocolversion\": xxxxx,   (numeric) the protocol version\n"
            "  \"walletversion\": xxxxx,     (numeric) the wallet version\n"
            "  \"balance\": xxxxxxx,         (numeric) the total blocknetdx balance of the wallet\n"
            "  \"obfuscation_balance\": xxxxxx, (numeric) the anonymized blocknetdx balance of the wallet\n"
            "  \"blocks\": xxxxxx,           (numeric) the current number of blocks processed in the server\n"
            "  \"timeoffset\": xxxxx,        (numeric) the time offset\n"
            "  \"connections\": xxxxx,       (numeric) the number of connections\n"
            "  \"proxy\": \"host:port\",     (string, optional) the proxy used by the server\n"
            "  \"difficulty\": xxxxxx,       (numeric) the current difficulty\n"
            "  \"testnet\": true|false,      (boolean) if the server is using testnet or not\n"
            "  \"keypoololdest\": xxxxxx,    (numeric) the timestamp (seconds since GMT epoch) of the oldest pre-generated key in the key pool\n"
            "  \"keypoolsize\": xxxx,        (numeric) how many new keys are pre-generated\n"
            "  \"unlocked_until\": ttt,      (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "  \"paytxfee\": x.xxxx,         (numeric) the transaction fee set in blocknetdx/kb\n"
            "  \"relayfee\": x.xxxx,         (numeric) minimum relay fee for non-free transactions in blocknetdx/kb\n"
            "  \"xrouter\": true|false,      (boolean) true if xrouter is enabled\n"
            "  \"staking status\": true|false,  (boolean) if the wallet is staking or not\n"
            "  \"errors\": \"...\"           (string) any error messages\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getinfo", "") + HelpExampleRpc("getinfo", ""));

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    Object obj;
    obj.push_back(Pair("version", CLIENT_VERSION));
    obj.push_back(Pair("protocolversion", PROTOCOL_VERSION));
    obj.emplace_back("xbridgeprotocolversion", static_cast<int64_t>(XBRIDGE_PROTOCOL_VERSION));
    obj.emplace_back("xrouterprotocolversion", static_cast<int64_t>(XROUTER_PROTOCOL_VERSION));

    LOCK(cs_main);

#ifdef ENABLE_WALLET
    if (pwalletMain) {
        {
        LOCK(pwalletMain->cs_wallet);
        obj.push_back(Pair("walletversion", pwalletMain->GetVersion()));
        }
        obj.push_back(Pair("balance", ValueFromAmount(pwalletMain->GetBalance())));
        if (!fLiteMode)
            obj.push_back(Pair("obfuscation_balance", ValueFromAmount(pwalletMain->GetAnonymizedBalance())));
    }
#endif
    obj.push_back(Pair("blocks", (int)chainActive.Height()));
    obj.push_back(Pair("timeoffset", GetTimeOffset()));
    obj.push_back(Pair("connections", (int)vNodes.size()));
    obj.push_back(Pair("proxy", (proxy.IsValid() ? proxy.ToStringIPPort() : string())));
    obj.push_back(Pair("difficulty", (double)GetDifficulty()));
    obj.push_back(Pair("testnet", Params().TestnetToBeDeprecatedFieldRPC()));
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        {
            LOCK(pwalletMain->cs_wallet);
            obj.push_back(Pair("keypoololdest", pwalletMain->GetOldestKeyPoolTime()));
            obj.push_back(Pair("keypoolsize", (int)pwalletMain->GetKeyPoolSize()));
            if (pwalletMain->IsCrypted())
                obj.push_back(Pair("unlocked_until", nWalletUnlockTime));
        }
    }
    obj.push_back(Pair("paytxfee", ValueFromAmount(payTxFee.GetFeePerK())));
#endif
    obj.push_back(Pair("relayfee", ValueFromAmount(::minRelayTxFee.GetFeePerK())));
    // Is xrouter enabled
    obj.push_back(Pair("xrouter", xrouter::App::isEnabled()));
    bool nStaking = false;
    if (mapHashedBlocks.count(chainActive.Tip()->nHeight))
        nStaking = true;
    else if (mapHashedBlocks.count(chainActive.Tip()->nHeight - 1) && nLastCoinStakeSearchInterval)
        nStaking = true;
    obj.push_back(Pair("staking status", (nStaking ? "Staking Active" : "Staking Not Active")));
    obj.push_back(Pair("errors", GetWarnings("statusbar")));
    return obj;
}

Value mnsync(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "mnsync [status|reset]\n"
            "Returns the sync status or resets sync.\n");

    std::string strMode = params[0].get_str();

    if (strMode == "status") {
        Object obj;

        obj.push_back(Pair("IsBlockchainSynced", servicenodeSync.IsBlockchainSynced()));
        obj.push_back(Pair("lastServicenodeList", servicenodeSync.lastServicenodeList));
        obj.push_back(Pair("lastServicenodeWinner", servicenodeSync.lastServicenodeWinner));
        obj.push_back(Pair("lastBudgetItem", servicenodeSync.lastBudgetItem));
        obj.push_back(Pair("lastFailure", servicenodeSync.lastFailure));
        obj.push_back(Pair("nCountFailures", servicenodeSync.nCountFailures));
        obj.push_back(Pair("sumServicenodeList", servicenodeSync.sumServicenodeList));
        obj.push_back(Pair("sumServicenodeWinner", servicenodeSync.sumServicenodeWinner));
        obj.push_back(Pair("sumBudgetItemProp", servicenodeSync.sumBudgetItemProp));
        obj.push_back(Pair("sumBudgetItemFin", servicenodeSync.sumBudgetItemFin));
        obj.push_back(Pair("countServicenodeList", servicenodeSync.countServicenodeList));
        obj.push_back(Pair("countServicenodeWinner", servicenodeSync.countServicenodeWinner));
        obj.push_back(Pair("countBudgetItemProp", servicenodeSync.countBudgetItemProp));
        obj.push_back(Pair("countBudgetItemFin", servicenodeSync.countBudgetItemFin));
        obj.push_back(Pair("RequestedServicenodeAssets", servicenodeSync.RequestedServicenodeAssets));
        obj.push_back(Pair("RequestedServicenodeAttempt", servicenodeSync.RequestedServicenodeAttempt));


        return obj;
    }

    if (strMode == "reset") {
        servicenodeSync.Reset();
        return "success";
    }
    return "failure";
}

#ifdef ENABLE_WALLET
class DescribeAddressVisitor : public boost::static_visitor<Object>
{
private:
    isminetype mine;

public:
    DescribeAddressVisitor(isminetype mineIn) : mine(mineIn) {}

    Object operator()(const CNoDestination& /*dest*/) const { return Object(); }

    Object operator()(const CKeyID& keyID) const
    {
        Object obj;
        CPubKey vchPubKey;
        obj.push_back(Pair("isscript", false));
        if (mine == ISMINE_SPENDABLE) {
            pwalletMain->GetPubKey(keyID, vchPubKey);
            obj.push_back(Pair("pubkey", HexStr(vchPubKey)));
            obj.push_back(Pair("iscompressed", vchPubKey.IsCompressed()));
        }
        return obj;
    }

    Object operator()(const CScriptID& scriptID) const
    {
        Object obj;
        obj.push_back(Pair("isscript", true));
        if (mine != ISMINE_NO) {
            CScript subscript;
            pwalletMain->GetCScript(scriptID, subscript);
            std::vector<CTxDestination> addresses;
            txnouttype whichType;
            int nRequired;
            ExtractDestinations(subscript, whichType, addresses, nRequired);
            obj.push_back(Pair("script", GetTxnOutputType(whichType)));
            obj.push_back(Pair("hex", HexStr(subscript.begin(), subscript.end())));
            Array a;
            BOOST_FOREACH (const CTxDestination& addr, addresses)
                a.push_back(CBitcoinAddress(addr).ToString());
            obj.push_back(Pair("addresses", a));
            if (whichType == TX_MULTISIG)
                obj.push_back(Pair("sigsrequired", nRequired));
        }
        return obj;
    }
};
#endif

/*
    Used for updating/reading spork settings on the network
*/
Value spork(const Array& params, bool /*fHelp*/)
{
    if (params.size() == 1 && params[0].get_str() == "show") {
        Object ret;
        for (int nSporkID = SPORK_START; nSporkID <= SPORK_END; nSporkID++) {
            if (sporkManager.GetSporkNameByID(nSporkID) != "Unknown")
                ret.push_back(Pair(sporkManager.GetSporkNameByID(nSporkID), GetSporkValue(nSporkID)));
        }
        return ret;
    } else if (params.size() == 1 && params[0].get_str() == "active") {
        Object ret;
        for (int nSporkID = SPORK_START; nSporkID <= SPORK_END; nSporkID++) {
            if (sporkManager.GetSporkNameByID(nSporkID) != "Unknown")
                ret.push_back(Pair(sporkManager.GetSporkNameByID(nSporkID), IsSporkActive(nSporkID)));
        }
        return ret;
    } else if (params.size() == 2) {
        int nSporkID = sporkManager.GetSporkIDByName(params[0].get_str());
        if (nSporkID == -1) {
            return "Invalid spork name";
        }

        // SPORK VALUE
        int64_t nValue = params[1].get_int();

        //broadcast new spork
        if (sporkManager.UpdateSpork(nSporkID, nValue)) {
            ExecuteSpork(nSporkID, nValue);
            return "success";
        } else {
            return "failure";
        }
    }

    throw runtime_error(
        "spork <name> [<value>]\n"
        "<name> is the corresponding spork name, or 'show' to show all current spork settings, active to show which sporks are active"
        "<value> is a epoch datetime to enable or disable spork" +
        HelpRequiringPassphrase());
}

Value validateaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "validateaddress \"blocknetaddress\"\n"
            "\nReturn information about the given blocknetdx address.\n"
            "\nArguments:\n"
            "1. \"blocknetaddress\"     (string, required) The blocknetdx address to validate\n"
            "\nResult:\n"
            "{\n"
            "  \"isvalid\" : true|false,         (boolean) If the address is valid or not. If not, this is the only property returned.\n"
            "  \"address\" : \"blocknetaddress\", (string) The blocknetdx address validated\n"
            "  \"ismine\" : true|false,          (boolean) If the address is yours or not\n"
            "  \"isscript\" : true|false,        (boolean) If the key is a script\n"
            "  \"pubkey\" : \"publickeyhex\",    (string) The hex value of the raw public key\n"
            "  \"iscompressed\" : true|false,    (boolean) If the address is compressed\n"
            "  \"account\" : \"account\"         (string) The account associated with the address, \"\" is the default account\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"") + HelpExampleRpc("validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\""));

    CBitcoinAddress address(params[0].get_str());
    bool isValid = address.IsValid();

    Object ret;
    ret.push_back(Pair("isvalid", isValid));
    if (isValid) {
        CTxDestination dest = address.Get();
        string currentAddress = address.ToString();
        ret.push_back(Pair("address", currentAddress));
#ifdef ENABLE_WALLET
        {
        LOCK(cs_main);
        isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : ISMINE_NO;
        ret.push_back(Pair("ismine", (mine & ISMINE_SPENDABLE) ? true : false));
        if (mine != ISMINE_NO) {
            ret.push_back(Pair("iswatchonly", (mine & ISMINE_WATCH_ONLY) ? true : false));
            Object detail = boost::apply_visitor(DescribeAddressVisitor(mine), dest);
            ret.insert(ret.end(), detail.begin(), detail.end());
        }
        {
        LOCK(pwalletMain->cs_wallet);
        if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
            ret.push_back(Pair("account", pwalletMain->mapAddressBook[dest].name));
        }
        }
#endif
    }
    return ret;
}

/**
 * Used by addmultisigaddress / createmultisig:
 */
CScript _createmultisig_redeemScript(const Array& params)
{
    int nRequired = params[0].get_int();
    const Array& keys = params[1].get_array();

    // Gather public keys
    if (nRequired < 1)
        throw runtime_error("a multisignature address must require at least one key to redeem");
    if ((int)keys.size() < nRequired)
        throw runtime_error(
            strprintf("not enough keys supplied "
                      "(got %u keys, but need at least %d to redeem)",
                keys.size(), nRequired));
    if (keys.size() > 16)
        throw runtime_error("Number of addresses involved in the multisignature address creation > 16\nReduce the number");
    std::vector<CPubKey> pubkeys;
    pubkeys.resize(keys.size());
    for (unsigned int i = 0; i < keys.size(); i++) {
        const std::string& ks = keys[i].get_str();
#ifdef ENABLE_WALLET
        // Case 1: BlocknetDX address and we have full public key:
        CBitcoinAddress address(ks);
        if (pwalletMain && address.IsValid()) {
            CKeyID keyID;
            if (!address.GetKeyID(keyID))
                throw runtime_error(
                    strprintf("%s does not refer to a key", ks));
            CPubKey vchPubKey;
            if (!pwalletMain->GetPubKey(keyID, vchPubKey))
                throw runtime_error(
                    strprintf("no full public key for address %s", ks));
            if (!vchPubKey.IsFullyValid())
                throw runtime_error(" Invalid public key: " + ks);
            pubkeys[i] = vchPubKey;
        }

        // Case 2: hex public key
        else
#endif
            if (IsHex(ks)) {
            CPubKey vchPubKey(ParseHex(ks));
            if (!vchPubKey.IsFullyValid())
                throw runtime_error(" Invalid public key: " + ks);
            pubkeys[i] = vchPubKey;
        } else {
            throw runtime_error(" Invalid public key: " + ks);
        }
    }
    CScript result = GetScriptForMultisig(nRequired, pubkeys);

    if (result.size() > MAX_SCRIPT_ELEMENT_SIZE)
        throw runtime_error(
            strprintf("redeemScript exceeds size limit: %d > %d", result.size(), MAX_SCRIPT_ELEMENT_SIZE));

    return result;
}

Value createmultisig(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 2) {
        string msg = "createmultisig nrequired [\"key\",...]\n"
                     "\nCreates a multi-signature address with n signature of m keys required.\n"
                     "It returns a json object with the address and redeemScript.\n"

                     "\nArguments:\n"
                     "1. nrequired      (numeric, required) The number of required signatures out of the n keys or addresses.\n"
                     "2. \"keys\"       (string, required) A json array of keys which are blocknetdx addresses or hex-encoded public keys\n"
                     "     [\n"
                     "       \"key\"    (string) blocknetdx address or hex-encoded public key\n"
                     "       ,...\n"
                     "     ]\n"

                     "\nResult:\n"
                     "{\n"
                     "  \"address\":\"multisigaddress\",  (string) The value of the new multisig address.\n"
                     "  \"redeemScript\":\"script\"       (string) The string value of the hex-encoded redemption script.\n"
                     "}\n"

                     "\nExamples:\n"
                     "\nCreate a multisig address from 2 addresses\n" +
                     HelpExampleCli("createmultisig", "2 \"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"") +
                     "\nAs a json rpc call\n" + HelpExampleRpc("createmultisig", "2, \"[\\\"16sSauSf5pF2UkUwvKGq4qjNRzBZYqgEL5\\\",\\\"171sgjn4YtPu27adkKGrdDwzRTxnRkBfKV\\\"]\"");
        throw runtime_error(msg);
    }

    // Construct using pay-to-script-hash:
    CScript inner = _createmultisig_redeemScript(params);
    CScriptID innerID(inner);
    CBitcoinAddress address(innerID);

    Object result;
    result.push_back(Pair("address", address.ToString()));
    result.push_back(Pair("redeemScript", HexStr(inner.begin(), inner.end())));

    return result;
}

Value verifymessage(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "verifymessage \"blocknetaddress\" \"signature\" \"message\"\n"
            "\nVerify a signed message\n"
            "\nArguments:\n"
            "1. \"blocknetaddress\"  (string, required) The blocknetdx address to use for the signature.\n"
            "2. \"signature\"       (string, required) The signature provided by the signer in base 64 encoding (see signmessage).\n"
            "3. \"message\"         (string, required) The message that was signed.\n"
            "\nResult:\n"
            "true|false   (boolean) If the signature is verified or not.\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n" +
            HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n" + HelpExampleCli("signmessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"my message\"") +
            "\nVerify the signature\n" + HelpExampleCli("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\" \"signature\" \"my message\"") +
            "\nAs json rpc\n" + HelpExampleRpc("verifymessage", "\"1D1ZrZNe3JUo7ZycKEYQQiQAWd9y54F4XZ\", \"signature\", \"my message\""));

    string strAddress = params[0].get_str();
    string strSign = params[1].get_str();
    string strMessage = params[2].get_str();

    CBitcoinAddress addr(strAddress);
    if (!addr.IsValid())
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");

    CKeyID keyID;
    if (!addr.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");

    bool fInvalid = false;
    vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CHashWriter ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == keyID);
}

Value setmocktime(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "setmocktime timestamp\n"
            "\nSet the local time to given timestamp (-regtest only)\n"
            "\nArguments:\n"
            "1. timestamp  (integer, required) Unix seconds-since-epoch timestamp\n"
            "   Pass 0 to go back to using the system time.");

    if (!Params().MineBlocksOnDemand())
        throw runtime_error("setmocktime for regression testing (-regtest mode) only");

    RPCTypeCheck(params, boost::assign::list_of(int_type));
    SetMockTime(params[0].get_int64());

    return Value::null;
}

#ifdef ENABLE_WALLET
Value getstakingstatus(const Array & params, bool fHelp)
{
    if (fHelp)
    {
        throw runtime_error(
            "getstakingstatus\n"
            "Returns an object containing various staking information.\n"
            "\nResult:\n"
            "{\n"
            "  \"validtime\": true|false,          (boolean) if the chain tip is within staking phases\n"
            "  \"haveconnections\": true|false,    (boolean) if network connections are present\n"
            "  \"walletunlocked\": true|false,     (boolean) if the wallet is unlocked\n"
            "  \"mintablecoins\": true|false,      (boolean) if the wallet has mintable coins\n"
            "  \"enoughcoins\": true|false,        (boolean) if available coins are greater than reserve balance\n"
            "  \"mnsync\": true|false,             (boolean) if servicenode data is synced\n"
            "  \"staking status\": true|false,     (boolean) if the wallet is staking or not\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("getstakingstatus", "") + HelpExampleRpc("getstakingstatus", ""));
    }

    Object obj;
    obj.push_back(Pair("validtime", chainActive.Tip()->nTime > 1471482000));
    obj.push_back(Pair("haveconnections", !vNodes.empty()));
    if (pwalletMain) {
        obj.push_back(Pair("walletunlocked", !pwalletMain->IsLocked()));
        obj.push_back(Pair("mintablecoins", pwalletMain->MintableCoins()));
        obj.push_back(Pair("enoughcoins", nReserveBalance <= pwalletMain->GetBalance()));
    }
    obj.push_back(Pair("mnsync", servicenodeSync.IsSynced()));

    bool nStaking = false;
    if (mapHashedBlocks.count(chainActive.Tip()->nHeight))
        nStaking = true;
    else if (mapHashedBlocks.count(chainActive.Tip()->nHeight - 1) && nLastCoinStakeSearchInterval)
        nStaking = true;
    obj.push_back(Pair("staking status", nStaking));

    return obj;
}
#endif // ENABLE_WALLET

#ifdef ENABLE_WALLET
Value gettradingdata(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "gettradingdata blocks errors\n"
            "Returns an object containing xbridge trading records.\n"
            "\nArguments:\n"
            "1. blocks  (integer, optional) count of blocks for search\n"
            "2. errors  (bool, optional, default: false) show errors\n"
            "\nResult:\n"
            "{\n"
            "  \"timestamp\":  \"timestamp\",       (uint64) block date in unixtime format\n"
            "  \"txid\":       \"transaction id\",  (string) blocknet transaction id\n"
            "  \"to\":         \"address\",         (string) receiver address\n"
            "  \"xid\":        \"transaction id\",  (string) xbridge transaction id\n"
            "  \"from\":       \"XXX\",             (string) from currency\n"
            "  \"fromAmount\": 0,                   (uint64) from amount\n"
            "  \"to\":         \"XXX\",             (string) to currency\n"
            "  \"toAmount\":   0,                   (uint64) toAmount\n"
            "}\n"
            "\nExamples:\n" +
            HelpExampleCli("gettradingdata", "") + HelpExampleRpc("gettradingdata", ""));

    uint32_t countOfBlocks = std::numeric_limits<uint32_t>::max();
    if (params.size() >= 1)
    {
        RPCTypeCheck(params, boost::assign::list_of(int_type));
        countOfBlocks = params[0].get_int();
    }
    bool showErrors = false;
    if (params.size() == 2) {
        RPCTypeCheck(params, boost::assign::list_of(bool_type));
        showErrors = params[1].get_bool();
    }

    LOCK(cs_main);

    Array records;

    CBlockIndex * pindex = chainActive.Tip();
    int64_t timeBegin = chainActive.Tip()->GetBlockTime();
    for (; pindex->pprev && pindex->GetBlockTime() > (timeBegin-30*24*60*60) && countOfBlocks > 0;
             pindex = pindex->pprev, --countOfBlocks)
    {
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex))
        {
            // throw
            continue;
        }
        const auto timestamp = block.GetBlockTime();
        for (const CTransaction & tx : block.vtx)
        {
            const auto txid = tx.GetHash().GetHex();
            std::string snode_pubkey{};

            const CurrencyPair p = TxOutToCurrencyPair(tx.vout, snode_pubkey);
            switch(p.tag) {
            case CurrencyPair::Tag::Error:
                // Show errors
                if (showErrors)
                    records.emplace_back(Object{
                        Pair{"timestamp",  timestamp},
                        Pair{"txid",       txid},
                        Pair{"xid",        p.error()}
                    });
                break;
            case CurrencyPair::Tag::Valid:
                records.emplace_back(Object{
                            Pair{"timestamp",  timestamp},
                            Pair{"txid",       txid},
                            Pair{"to",         snode_pubkey},
                            Pair{"xid",        p.xid()},
                            Pair{"from",       p.from.currency().to_string()},
                            Pair{"fromAmount", p.from.amount<double>()},
                            Pair{"to",         p.to.currency().to_string()},
                            Pair{"toAmount",   p.to.amount<double>()},
                            });
                break;
            case CurrencyPair::Tag::Empty:
            default:
                break;
            }
        }
    }

    return records;
}
#endif // ENABLE_WALLET
