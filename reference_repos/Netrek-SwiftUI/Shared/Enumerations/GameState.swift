//
//  GameState.swift
//  Netrek2
//
//  Created by Darrell Root on 5/5/20.
//  Copyright © 2020 Darrell Root. All rights reserved.
//

import Foundation

//Whenever gameState changes, gameScreen matches
//But we can manually change gameScreen to go to help or credits without changing gameState

enum GameState: String, CaseIterable {
    case noServerSelected
    case serverSelected
    case serverConnected
    case serverSlotFound
    case loginAccepted
    case gameActive
}
