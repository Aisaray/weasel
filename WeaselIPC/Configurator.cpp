module;
#include "stdafx.h"
module Configurator;
import Deserializer;
import Configurator;

using namespace weasel;

Deserializer::Ptr Configurator::Create(ResponseParser* pTarget)
{
	return Deserializer::Ptr(new Configurator(pTarget));
}

Configurator::Configurator(ResponseParser* pTarget)
	: Deserializer(pTarget)
{
}

Configurator::~Configurator()
{
}

void Configurator::Store(const Deserializer::KeyType& key, std::wstring const& value)
{
	if (!m_pTarget->p_context || key.size() < 2)
		return;
	bool bool_value = (!value.empty() && value != L"0");
	if (key[1] == L"inline_preedit")
	{
		m_pTarget->p_config->inline_preedit = bool_value;
	}
}
