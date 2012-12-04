/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012 Icinga Development Team (http://www.icinga.org/)        *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "i2-icinga.h"

using namespace icinga;

static AttributeDescription hostGroupAttributes[] = {
	{ "alias", Attribute_Config },
	{ "notes_url", Attribute_Config },
	{ "action_url", Attribute_Config }
};

REGISTER_TYPE(HostGroup, hostGroupAttributes);

String HostGroup::GetAlias(void) const
{
	String value = Get("alias");

	if (!value.IsEmpty())
		return value;
	else
		return GetName();
}

String HostGroup::GetNotesUrl(void) const
{
	return Get("notes_url");
}

String HostGroup::GetActionUrl(void) const
{
	return Get("action_url");
}

bool HostGroup::Exists(const String& name)
{
	return (DynamicObject::GetObject("HostGroup", name));
}

HostGroup::Ptr HostGroup::GetByName(const String& name)
{
	DynamicObject::Ptr configObject = DynamicObject::GetObject("HostGroup", name);

	if (!configObject)
		throw_exception(invalid_argument("HostGroup '" + name + "' does not exist."));

	return dynamic_pointer_cast<HostGroup>(configObject);
}

