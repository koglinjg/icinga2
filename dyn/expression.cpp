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

#include "i2-dyn.h"

using namespace icinga;

Expression::Expression(string key, ExpressionOperator op, Variant value, long debuginfo)
	: Key(key), Operator(op), Value(value), DebugInfo(debuginfo)
{
}

void Expression::Execute(const Dictionary::Ptr& dictionary) const
{
	Variant oldValue, newValue;
	dictionary->GetProperty(Key, &oldValue);

	ExpressionList::Ptr exprl;
	if (Value.GetType() == VariantObject)
		exprl = dynamic_pointer_cast<ExpressionList>(Value.GetObject());

	newValue = Value;

	switch (Operator) {
		case OperatorSet:
			if (exprl) {
				Dictionary::Ptr dict = make_shared<Dictionary>();
				exprl->Execute(dict);
				newValue = dict;
			}

			break;

		case OperatorPlus:
			if (exprl) {
				Dictionary::Ptr dict;
				if (oldValue.GetType() == VariantObject)
					dict = dynamic_pointer_cast<Dictionary>(oldValue.GetObject());

				if (!dict) {
					if (!oldValue.IsEmpty()) {
						stringstream message;
						message << "Wrong argument types for += (non-dictionary and dictionary) (in line " << DebugInfo << ")";
						throw domain_error(message.str());
					}

					dict = make_shared<Dictionary>();
				}

				exprl->Execute(dict);
				newValue = dict;
			} else {
				stringstream message;
				message << "+= only works for dictionaries (in line " << DebugInfo << ")";
				throw domain_error(message.str());
			}

			break;

		default:
			break;
			//assert(!"Not yet implemented.");
	}

	dictionary->SetProperty(Key, newValue);
}
