/****************************************************************
 * @@@LICENSE
 *
 *  Copyright (c) 2013 LG Electronics, Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * LICENSE@@@
 ****************************************************************/

/**
 *  @file LSUtils.h
 */

#ifndef __LSUTILS_H
#define __LSUTILS_H

#include <luna-service2/lunaservice.h>

namespace LS {
	struct Error : public LSError
	{
		Error() { LSErrorInit(this); }
		~Error() { reset(); }

		void reset() { LSErrorFree(this); }

	private:
		Error(const Error &);
		Error &operator=(const Error &);
	};

	/**
	 * Simple wrapper for smart message handling
	 */
	struct MessageRef
	{
		MessageRef(LSMessage *message) : ref(message)
		{ LSMessageRef(ref); }

		MessageRef(const MessageRef &messageRef) : ref(messageRef.ref)
		{ LSMessageRef(ref); }

		~MessageRef()
		{ LSMessageUnref(ref); }

		MessageRef &operator=(const MessageRef &messageRef)
		{
			LSMessageUnref(ref);
			ref = messageRef.ref;
			LSMessageRef(ref);
			return *this;
		}

		LSMessage *get() const { return ref; }

	private:
		LSMessage *ref;

	};
}

#endif
