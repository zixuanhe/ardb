/*
 * sets.cpp
 *
 *  Created on: 2013-4-2
 *      Author: wqy
 */

#include "rddb.hpp"
#include <tr1/unordered_set>
#include <tr1/unordered_map>

namespace rddb
{
	static bool DecodeSetMetaData(ValueObject& v, SetMetaValue& meta)
	{
		if (v.type != RAW)
		{
			return false;
		}
		return BufferHelper::ReadVarUInt32(*(v.v.raw), meta.size);
	}
	static void EncodeSetMetaData(ValueObject& v, SetMetaValue& meta)
	{
		v.type = RAW;
		if (v.v.raw == NULL)
		{
			v.v.raw = new Buffer(16);
		}
		BufferHelper::WriteVarUInt32(*(v.v.raw), meta.size);
	}

	int RDDB::SAdd(DBID db, const Slice& key, const Slice& value)
	{
		KeyObject k(key, SET_META);
		ValueObject v;
		SetMetaValue meta;
		if (0 == GetValue(db, k, v))
		{
			if (!DecodeSetMetaData(v, meta))
			{
				return ERR_INVALID_TYPE;
			}
		}
		SetKeyObject sk(key, value);
		ValueObject sv;
		if (0 != GetValue(db, sk, sv))
		{
			meta.size++;
			sv.type = EMPTY;
			BatchWriteGuard guard(GetDB(db));
			SetValue(db, sk, sv);
			EncodeSetMetaData(v, meta);
			return SetValue(db, k, v) == 0 ? 1 : -1;
		}
		return 0;
	}

	int RDDB::SCard(DBID db, const Slice& key)
	{
		KeyObject k(key, SET_META);
		ValueObject v;
		SetMetaValue meta;
		if (0 == GetValue(db, k, v))
		{
			if (!DecodeSetMetaData(v, meta))
			{
				return ERR_INVALID_TYPE;
			}
			return meta.size;
		}
		return ERR_NOT_EXIST;
	}

	int RDDB::SIsMember(DBID db, const Slice& key, const Slice& value)
	{
		SetKeyObject sk(key, value);
		ValueObject sv;
		if (0 != GetValue(db, sk, sv))
		{
			return ERR_NOT_EXIST;
		}
		return 1;
	}

	int RDDB::SRem(DBID db, const Slice& key, const Slice& value)
	{
		KeyObject k(key, SET_META);
		ValueObject v;
		SetMetaValue meta;
		if (0 == GetValue(db, k, v))
		{
			if (!DecodeSetMetaData(v, meta))
			{
				return ERR_INVALID_TYPE;
			}
		}
		SetKeyObject sk(key, value);
		ValueObject sv;
		if (0 != GetValue(db, sk, sv))
		{
			meta.size--;
			sv.type = EMPTY;
			BatchWriteGuard guard(GetDB(db));
			DelValue(db, sk);
			EncodeSetMetaData(v, meta);
			return SetValue(db, k, v) == 0 ? 1 : -1;
		}
		return 0;
	}

	int RDDB::SMembers(DBID db, const Slice& key, StringArray& values)
	{
		Slice empty;
		SetKeyObject sk(key, empty);
		Iterator* iter = FindValue(db, sk);
		while (iter != NULL && iter->Valid())
		{
			Slice tmpkey = iter->Key();
			KeyObject* kk = decode_key(tmpkey);
			if (NULL == kk || kk->type != SET_ELEMENT
			        || kk->key.compare(key) != 0)
			{
				DELETE(kk);
				break;
			}
			Slice tmpvalue = iter->Value();
			SetKeyObject* sek = (SetKeyObject*) kk;
			std::string tmp(sek->value.data(), sek->value.size());
			values.push_back(tmp);
			DELETE(kk);
			iter->Next();
		}
		return 0;
	}

	int RDDB::SClear(DBID db, const Slice& key)
	{
		Slice empty;
		SetKeyObject sk(key, empty);
		Iterator* iter = FindValue(db, sk);
		BatchWriteGuard guard(GetDB(db));
		while (iter != NULL && iter->Valid())
		{
			Slice tmpkey = iter->Key();
			KeyObject* kk = decode_key(tmpkey);
			if (NULL == kk || kk->type != SET_ELEMENT
			        || kk->key.compare(key) != 0)
			{
				DELETE(kk);
				break;
			}
			SetKeyObject* sek = (SetKeyObject*) kk;
			DelValue(db, *sek);
			DELETE(kk);
			iter->Next();
		}
		KeyObject k(key, SET_META);
		DelValue(db, k);
		return 0;
	}

	int RDDB::SDiff(DBID db, SliceArray& keys, StringArray& values)
	{
		if (keys.size() < 2)
		{
			return ERR_INVALID_ARGS;
		}
		typedef std::tr1::unordered_set<std::string> ValueSet;
		typedef std::tr1::unordered_map<std::string, ValueSet> KeyValueSet;
		StringArray vs;
		KeyValueSet kvs;
		SMembers(db, keys.front(), vs);
		for (int i = 1; i < keys.size(); i++)
		{
			Slice k = keys.at(i);
			std::string tmp(k.data(), k.size());
			StringArray vss;
			SMembers(db, k, vss);
			StringArray::iterator vit = vss.begin();
			while (vit != vss.end())
			{
				kvs[tmp].insert(*vit);
				vit++;
			}
		}
		if (vs.size() > 0)
		{
			StringArray::iterator it = vs.begin();
			while (it < vs.begin())
			{
				std::string& v = *it;
				bool found = false;
				for (int i = 1; i < keys.size(); i++)
				{
					Slice k = keys.at(i);
					std::string tmp(k.data(), k.size());
					if (kvs[tmp].find(v) != kvs[tmp].end())
					{
						found = true;
						break;
					}
				}
				if (!found)
				{
					values.push_back(v);
				}
				it++;
			}
		}
		return 0;
	}

	int RDDB::SDiffStore(DBID db, const Slice& dst, SliceArray& keys)
	{
		if (keys.size() < 2)
		{
			return ERR_INVALID_ARGS;
		}
		StringArray vs;
		if (0 == SDiff(db, keys, vs) && vs.size() > 0)
		{
			SClear(db, dst);
			BatchWriteGuard guard(GetDB(db));
			KeyObject k(dst, SET_META);
			ValueObject smv;
			SetMetaValue meta;
			StringArray::iterator it = vs.begin();
			while (it < vs.begin())
			{
				std::string& v = *it;
				Slice sv(v.c_str(), v.size());
				SetKeyObject sk(dst, sv);
				ValueObject empty;
				empty.type = EMPTY;
				SetValue(db, sk, empty);
				meta.size++;
				it++;
			}
			EncodeSetMetaData(smv, meta);
			return SetValue(db, k, smv) == 0 ? meta.size : -1;
		}
		return 0;

	}

	int RDDB::SInter(DBID db, SliceArray& keys, StringArray& values)
	{
		if (keys.size() < 2)
		{
			return ERR_INVALID_ARGS;
		}
		typedef std::tr1::unordered_set<std::string> ValueSet;
		typedef std::tr1::unordered_map<std::string, ValueSet> KeyValueSet;
		StringArray vs;
		KeyValueSet kvs;
		SMembers(db, keys.front(), vs);
		for (int i = 1; i < keys.size(); i++)
		{
			Slice k = keys.at(i);
			std::string tmp(k.data(), k.size());
			StringArray vss;
			SMembers(db, k, vss);
			StringArray::iterator vit = vss.begin();
			while (vit != vss.end())
			{
				kvs[tmp].insert(*vit);
				vit++;
			}
		}
		if (vs.size() > 0)
		{
			StringArray::iterator it = vs.begin();
			while (it < vs.begin())
			{
				std::string& v = *it;
				bool found = true;
				for (int i = 1; i < keys.size(); i++)
				{
					Slice k = keys.at(i);
					std::string tmp(k.data(), k.size());
					if (kvs[tmp].find(v) == kvs[tmp].end())
					{
						found = false;
						break;
					}
				}
				if (found)
				{
					values.push_back(v);
				}
				it++;
			}
		}
		return 0;
	}

	int RDDB::SInterStore(DBID db, const Slice& dst, SliceArray& keys)
	{
		if (keys.size() < 2)
		{
			return ERR_INVALID_ARGS;
		}
		StringArray vs;
		if (0 == SInter(db, keys, vs) && vs.size() > 0)
		{
			SClear(db, dst);
			BatchWriteGuard guard(GetDB(db));
			KeyObject k(dst, SET_META);
			ValueObject smv;
			SetMetaValue meta;
			StringArray::iterator it = vs.begin();
			while (it < vs.begin())
			{
				std::string& v = *it;
				Slice sv(v.c_str(), v.size());
				SetKeyObject sk(dst, sv);
				ValueObject empty;
				empty.type = EMPTY;
				SetValue(db, sk, empty);
				meta.size++;
				it++;
			}
			EncodeSetMetaData(smv, meta);
			return SetValue(db, k, smv) == 0 ? meta.size : -1;
		}
		return 0;
	}

	int RDDB::SMove(DBID db, const Slice& src, const Slice& dst,
	        const Slice& value)
	{
		SetKeyObject sk(src, value);
		ValueObject sv;
		if (0 != GetValue(db, sk, sv))
		{
			return 0;
		}
		sk.key = dst;
		SetValue(db, sk, sv);
		return 1;
	}

	int RDDB::SPop(DBID db, const Slice& key, std::string& value)
	{
		Slice empty;
		SetKeyObject sk(key, empty);
		Iterator* iter = FindValue(db, sk);
		while (iter != NULL && iter->Valid())
		{
			Slice tmpkey = iter->Key();
			KeyObject* kk = decode_key(tmpkey);
			if (NULL == kk || kk->type != SET_ELEMENT
			        || kk->key.compare(key) != 0)
			{
				DELETE(kk);
				break;
			}
			SetKeyObject* sek = (SetKeyObject*) kk;
			value.append(sek->value.data(), sek->value.size());
			DelValue(db, *sek);
			DELETE(kk);
			return 0;
		}
		return -1;
	}

	int RDDB::SRandMember(DBID db, const Slice& key, StringArray& values,
	        int count)
	{
		Slice empty;
		SetKeyObject sk(key, empty);
		Iterator* iter = FindValue(db, sk);
		int total = count;
		if (count < 0)
		{
			total = 0 - count;
		}
		int cursor = 0;
		while (iter != NULL && iter->Valid())
		{
			Slice tmpkey = iter->Key();
			KeyObject* kk = decode_key(tmpkey);
			if (NULL == kk || kk->type != SET_ELEMENT
			        || kk->key.compare(key) != 0)
			{
				DELETE(kk);
				break;
			}
			SetKeyObject* sek = (SetKeyObject*) kk;
			values.push_back(std::string(sek->value.data(), sek->value.size()));
			DELETE(kk);
			iter->Next();
			cursor++;
			if (cursor == total)
			{
				break;
			}
		}
		DELETE(iter);
		while (count < 0 && values.size() < total)
		{
			values.push_back(values.front());
		}
		return 0;
	}

	int RDDB::SUnion(DBID db, SliceArray& keys, StringArray& values)
	{
		typedef std::tr1::unordered_set<std::string> ValueSet;
		ValueSet kvs;
		for (int i = 0; i < keys.size(); i++)
		{
			Slice k = keys.at(i);
			std::string tmp(k.data(), k.size());
			StringArray vss;
			SMembers(db, k, vss);
			StringArray::iterator vit = vss.begin();
			while (vit != vss.end())
			{
				kvs.insert(*vit);
				vit++;
			}
		}
		ValueSet::iterator vit = kvs.begin();
		while (vit != kvs.end())
		{
			values.push_back(*vit);
			vit++;
		}
		return 0;
	}

	int RDDB::SUnionStore(DBID db, const Slice& dst, SliceArray& keys)
	{
		StringArray ss;
		if (0 == SUnion(db, keys, ss) && ss.size() > 0)
		{
			SClear(db, dst);
			BatchWriteGuard guard(GetDB(db));
			KeyObject k(dst, SET_META);
			ValueObject smv;
			SetMetaValue meta;
			StringArray::iterator it = ss.begin();
			while (it < ss.begin())
			{
				std::string& v = *it;
				Slice sv(v.c_str(), v.size());
				SetKeyObject sk(dst, sv);
				ValueObject empty;
				empty.type = EMPTY;
				SetValue(db, sk, empty);
				meta.size++;
				it++;
			}
			EncodeSetMetaData(smv, meta);
			return SetValue(db, k, smv) == 0 ? meta.size : -1;
		}
		return 0;
	}

}
