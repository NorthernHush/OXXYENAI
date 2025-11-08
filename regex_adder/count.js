import fs from 'fs';

const PATH = '../memory.txt';

async function main() {
    let regex = /\{\s*"instruction"\s*:\s*".*?"\s*,\s*"output"\s*:\s*".*?"\s*\}/
    let count = 0;
    let data;

    try {
        data = fs.readFileSync(PATH, 'utf8');  
        data = data.split(regex);    // >> разбиваем по блокам (каждый блок = индекс)

        for (let i = 0; i < data.length; i++) {
            count++;
        }

        console.log(count)

    } catch (error) {
        console.error("Не удалось посчитать кол-во промтов: ", error);
        throw error;
    }
}

main();
